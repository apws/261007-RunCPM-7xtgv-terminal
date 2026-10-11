"""Shared row policy and real shell launcher wiring, without GUI/card access."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import generate_key_events as generator
from host_key_config import TOPROW_SETTING, keyboard_environment


class HostKeyConfigTests(unittest.TestCase):
    def test_default_generation_preserves_existing_profiles(self):
        konsole, kitty = generator.generate('+ěščřžýáíé')
        for name, rules in [('7xtgv-terminal.keytab', konsole), ('kitty.conf', kitty), ('kitty-cpm.conf', kitty)]:
            source = TOOLS / name
            self.assertEqual(generator.rendered(source, rules), source.read_text())

    def test_disabling_removes_only_number_row_events(self):
        enabled = generator.generate('+ěščřžýáíé')
        disabled = generator.generate('')
        for full, empty in zip(enabled, disabled):
            self.assertEqual(full[20:], empty)
            self.assertEqual(len(full) - len(empty), 20)
        # The rest of each template, including brightness and Alt keys, is untouched.
        for name, rules in [('7xtgv-terminal.keytab', disabled[0]), ('kitty.conf', disabled[1])]:
            original = (TOOLS / name).read_text()
            rendered = generator.rendered(TOOLS / name, rules)
            self.assertEqual(original.split(generator.BEGIN)[0], rendered.split(generator.BEGIN)[0])
            self.assertEqual(original.split(generator.END)[1], rendered.split(generator.END)[1])

    def test_alternative_row_and_setting_precedence(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'terminal.conf'
            config.write_text("XTGV_HACK_TOPROW='+ľščťžýáíé'\nXTGV_BANK_KEYS='different'\n")
            row = keyboard_environment(config, {})[TOPROW_SETTING]
            konsole, kitty = generator.generate(row)
            self.assertIn('ľ', konsole[2])
            self.assertIn('ľ', kitty[2])
            self.assertNotIn('ě', '\n'.join(konsole[:20]))
            self.assertEqual(keyboard_environment(config, {TOPROW_SETTING: ''})[TOPROW_SETTING], '')

    def test_unsupported_or_conflicting_row_is_reported(self):
        for row in ('+ěščřžýáí#', '+ěščřžýáí ', '+ěščřžýáíě'):
            with self.assertRaisesRegex(RuntimeError, TOPROW_SETTING):
                generator.generate(row)

    def test_cached_variants_preserve_previous_content(self):
        with tempfile.TemporaryDirectory() as directory:
            for host, suffix in [('konsole', '.keytab'), ('kitty', '.conf')]:
                first = generator.launch_config(host, directory, '+ěščřžýáíé')
                path = Path(directory) / (first + suffix) if host == 'konsole' else Path(first)
                content = path.read_bytes()
                second = generator.launch_config(host, directory, '')
                self.assertNotEqual(first, second)
                self.assertEqual(path.read_bytes(), content)
                self.assertEqual(first, generator.launch_config(host, directory, '+ěščřžýáíé'))

    def test_shell_launchers_use_selected_immutable_config(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary_dir = root / 'bin'
            binary_dir.mkdir()
            capture = root / 'args.json'
            for host in ('konsole', 'kitty'):
                fake = binary_dir / host
                fake.write_text('#!/usr/bin/env python3\nimport json, os, sys\nfrom pathlib import Path\nPath(os.environ["XTGV_TEST_ARGS"]).write_text(json.dumps(sys.argv[1:]))\n')
                fake.chmod(0o755)
            for host, launcher in [('konsole', 'kon.sh'), ('kitty', 'kit.sh')]:
                paths = []
                for row in ('+ěščřžýáíé', ''):
                    env = dict(os.environ, PATH=str(binary_dir) + os.pathsep + os.environ['PATH'],
                               XDG_DATA_HOME=str(root / 'data'), XDG_CACHE_HOME=str(root / 'cache'),
                               XTGV_TEST_ARGS=str(capture), XTGV_HACK_TOPROW=row)
                    subprocess.run(['sh', str(TOOLS / launcher), '--desktop-pty', '--port', '/unused'],
                                   env=env, check=True, capture_output=True)
                    args = json.loads(capture.read_text())
                    self.assertEqual(args[-3:], ['--desktop-pty', '--port', '/unused'])
                    if host == 'konsole':
                        value = args[args.index('-p') + 1].removeprefix('KeyBindings=')
                        path = root / 'data/konsole' / (value + '.keytab')
                    else:
                        path = Path(args[args.index('--config') + 1])
                    paths.append(path)
                    self.assertEqual('196155v' in path.read_text(), bool(row))
                self.assertNotEqual(*paths)
                self.assertIn('196155v', paths[0].read_text())


if __name__ == '__main__':
    unittest.main()
