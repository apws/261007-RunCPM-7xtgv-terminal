"""Source optimization routing without MCU compilation or device access."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SKETCH = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SKETCH / 'tools'))
import target_config
spec = importlib.util.spec_from_file_location('desktop_cli', SKETCH / 'desktop/desktop.py')
desktop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)


class OptimizationTests(unittest.TestCase):
    def test_all_board_properties_and_levels(self):
        for path in (SKETCH / 'tools/targets').glob('*.json'):
            definition = json.loads(path.read_text())
            key = ('build.flags.optimize' if path.stem.startswith('rp2350-')
                   else 'compiler.optimization_flags')
            for level in target_config.OPTIMIZATION_LEVELS:
                with self.subTest(target=path.stem, level=level):
                    expected = {} if level == 'default' else {key: '-' + level}
                    self.assertEqual(target_config.optimization_overrides(definition, level), expected)
        with self.assertRaises(RuntimeError):
            target_config.optimization_overrides({}, 'O2')

    def test_hardware_wrappers_forward_only_to_build(self):
        for folder in (SKETCH / 'tools/targets').glob('* -- *'):
            result = subprocess.run([str(folder / 'rebuild.sh'), '-O2', '--dry-run'],
                                    text=True, capture_output=True, check=True)
            self.assertIn('--build-only --optimization O2', result.stdout)
            result = subprocess.run([str(folder / 'reflash.sh'), '--optimization', 'O2', '--dry-run'],
                                    text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('not a reflash option', result.stderr)

    def test_desktop_cache_override_and_default_reset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for level in ('O2', 'O3', None, 'default'):
                args = SimpleNamespace(gfx=root, optimization=level, quiet=True, log=None)
                with patch.object(desktop, 'REPO', root), patch.object(desktop.subprocess, 'run') as run, \
                        contextlib.redirect_stdout(io.StringIO()):
                    desktop.build(args, 'jc8012p4a1c')
                expected = level if level not in (None, 'default') else 'O2'
                self.assertIn('-DXTGV_OPTIMIZATION=' + expected, run.call_args_list[0].args[0])

    def test_desktop_simple_rebuild_forwards_optimization(self):
        folder = SKETCH / 'tools/targets/EP4_10 -- jc8012p4a1c'
        result = subprocess.run([str(folder / 'desktop-rebuild.sh'), '-O2', '--dry-run'],
                                text=True, capture_output=True, check=True)
        self.assertIn('--optimization O2', result.stdout)
        result = subprocess.run([str(folder / 'desktop-local.sh'), '--optimization', 'O2', '--dry-run'],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('only for desktop-rebuild.sh', result.stderr)


if __name__ == '__main__': unittest.main()
