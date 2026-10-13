"""Incremental guide printing and preservation of reviewed PDFs on failure."""
import asyncio
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools-web'))
import print_guides as printer


class GuidePDFTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'guides/images').mkdir(parents=True)
        for name in ('01-BG-build-guide', '02-UG-user-guide'):
            (self.root/'guides'/f'{name}.adoc').write_text('= Test guide\n\nSome source content.\n')
        for name in ('asciidoctorjs-crx-mixworx.css', 'asciidoctorjs-crx-mixworx.js'):
            (self.root/name).write_text('shared formatting')
        self.output = self.root/'guides/pdf'
        self.batches = []

        async def render(jobs, stage, root):
            self.batches.append([job['name'] for job in jobs])
            for job in jobs:
                (stage/job['name']).write_bytes(b'%PDF-fixture-' + job['input_sha256'].encode())
            return dict(asciidoctor='fixture', chromium='fixture')

        self.renderer = AsyncMock(side_effect=render)
        for patcher in (patch.object(printer, 'BUILD', self.root/'build'),
                        patch.object(printer, 'render', self.renderer),
                        patch.object(printer, 'validate_pdf', return_value=1)):
            patcher.start()
            self.addCleanup(patcher.stop)

    async def update(self, **kwargs):
        return await printer.update(root=self.root, **kwargs)

    def saved(self):
        return {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.output.iterdir()}

    async def test_incremental_and_shared_inputs(self):
        self.assertEqual(await self.update(check=True), 1)
        self.assertFalse(self.output.exists())
        await self.update()
        before = self.saved()
        await self.update()
        self.assertEqual(self.saved(), before)
        self.assertEqual(len(self.batches), 1)
        self.assertEqual(await self.update(check=True), 0)
        (self.root/'guides/02-UG-user-guide.adoc').write_text('= Changed guide\n')
        await self.update()
        self.assertEqual(len(self.batches[-1]), 1)
        self.assertIn('02 UG', self.batches[-1][0])
        (self.root/'asciidoctorjs-crx-mixworx.css').write_text('changed CSS')
        await self.update()
        self.assertEqual(len(self.batches[-1]), 2)
        (self.root/'guides/images/diagram.svg').write_text('<svg/>')
        await self.update()
        self.assertEqual(len(self.batches[-1]), 2)

    async def test_missing_modified_and_force(self):
        await self.update()
        pdf = next(self.output.glob('*.pdf'))
        pdf.unlink()
        await self.update()
        self.assertEqual(self.batches[-1], [pdf.name])
        pdf.write_bytes(b'changed PDF')
        await self.update()
        self.assertEqual(self.batches[-1], [pdf.name])
        await self.update(force=True)
        self.assertEqual(len(self.batches[-1]), 2)

    async def test_print_profile_change_refreshes_every_guide(self):
        await self.update()
        before = self.saved()
        with patch.dict(printer.SETTINGS, scale=1.25):
            self.assertEqual(await self.update(check=True), 1)
            self.assertEqual(self.saved(), before)
            await self.update()
            self.assertEqual(len(self.batches[-1]), 2)
            self.assertEqual(json.loads((self.output/printer.MANIFEST).read_text())['settings']['scale'], 1.25)

    async def test_render_or_validation_failure_preserves_every_saved_file(self):
        await self.update()
        before = self.saved()
        with patch.object(printer, 'render', AsyncMock(side_effect=RuntimeError('print failed'))):
            with self.assertRaisesRegex(RuntimeError, 'print failed'):
                await self.update(force=True)
        self.assertEqual(self.saved(), before)
        with patch.object(printer, 'validate_pdf', side_effect=RuntimeError('bad PDF')):
            with self.assertRaisesRegex(RuntimeError, 'bad PDF'):
                await self.update(force=True)
        self.assertEqual(self.saved(), before)

    async def test_concurrent_source_edit_preserves_saved_files(self):
        await self.update()
        before = self.saved()
        original = self.renderer.side_effect
        async def edit_during_render(jobs, stage, root):
            result = await original(jobs, stage, root)
            (root/'guides/01-BG-build-guide.adoc').write_text('concurrent edit')
            return result
        self.renderer.side_effect = edit_during_render
        with self.assertRaisesRegex(RuntimeError, 'changed while printing'):
            await self.update(force=True)
        self.assertEqual(self.saved(), before)


class PDFPaperTests(unittest.TestCase):
    def test_validation_uses_selected_paper_size(self):
        def output(command, **kwargs):
            if command[0] == 'pdftotext':
                return 'Readable guide text. ' * 20
            if '-url' in command:
                return 'Page Type URL\n'
            return 'Pages: 2\nPage size: 612 x 1008 pts (legal)\n'
        with patch.object(printer.subprocess, 'check_output', side_effect=output):
            with patch.dict(printer.SETTINGS, format='Legal'):
                self.assertEqual(printer.validate_pdf(Path('guide.pdf')), 2)
            with patch.dict(printer.SETTINGS, format='A4'):
                with self.assertRaisesRegex(RuntimeError, 'not portrait A4'):
                    printer.validate_pdf(Path('guide.pdf'))


if __name__ == '__main__':
    unittest.main()
