import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import target_config as config


class TargetConfigTests(unittest.TestCase):
    def setUp(self):
        self.env = patch.dict(os.environ, {}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)

    def test_canonical_target_and_group_label(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            definition = {'method': 'arduino-cli', 'profile': 'example',
                          'display_id': 'GROUP_5', 'port': '/dev/example'}
            (folder / 'example.json').write_text(json.dumps(definition))
            name, data = config.load_target('example', folder)
            self.assertEqual(config.target_label(name, data), 'GROUP_5 (example)')
            self.assertEqual(config.resolve_port(target=name, definition=data), '/dev/example')
            for bad in ('../example', 'Example', 'unknown', ''):
                with self.assertRaises(RuntimeError): config.load_target(bad, folder)

    def test_port_precedence(self):
        data = {'port': 'data-port'}
        self.assertEqual(config.resolve_port('explicit', definition=data), 'explicit')
        with patch.dict(os.environ, {'XTGV_PORT': 'environment'}):
            self.assertEqual(config.resolve_port('explicit', definition=data), 'explicit')
            self.assertEqual(config.resolve_port(definition=data), 'environment')
        self.assertEqual(config.resolve_port(definition=data), 'data-port')

    def test_discovery_unique_ambiguous_and_absent(self):
        data = {'port_globs': ['pattern'], 'port': 'fallback'}
        with patch.object(config.glob, 'glob', return_value=['/dev/one']):
            self.assertEqual(config.resolve_port(target='example', definition=data), '/dev/one')
        with patch.object(config.glob, 'glob', return_value=['/dev/one', '/dev/two']):
            with self.assertRaises(RuntimeError): config.resolve_port(target='example', definition=data)
        with patch.object(config.glob, 'glob', return_value=[]):
            self.assertEqual(config.resolve_port(definition=data), 'fallback')
            with self.assertRaises(RuntimeError): config.resolve_port(definition={'port_globs': ['pattern']})

    def test_discovery_deduplicates_same_device(self):
        with patch.object(config.glob, 'glob', return_value=['/alias/a', '/alias/b']), \
                patch.object(config.os.path, 'realpath', return_value='/dev/same'):
            self.assertEqual(config.resolve_port(definition={'port_globs': ['pattern']}), '/alias/a')

    def test_explicit_port_needs_no_target(self):
        self.assertEqual(config.resolve_port('/dev/manual'), '/dev/manual')
        with self.assertRaises(RuntimeError): config.resolve_port()

    def test_cli_lookup_configuration_and_path(self):
        with patch.object(config.shutil, 'which', return_value='/tools/arduino-cli') as which:
            self.assertEqual(config.arduino_cli(), '/tools/arduino-cli')
            which.assert_called_with('arduino-cli')
            with patch.dict(os.environ, {'ARDUINO_CLI': '/custom/cli'}):
                config.arduino_cli()
                which.assert_called_with('/custom/cli')
        with patch.dict(os.environ, {'ARDUINO_CLI': '/missing/cli'}), \
                patch.object(config.shutil, 'which', return_value=None):
            with self.assertRaises(RuntimeError): config.arduino_cli()

    def test_current_data_has_profile_ids(self):
        for path in config.TARGETS.glob('*.json'):
            name, data = config.load_target(path.stem)
            self.assertTrue(data['display_id'])
            self.assertTrue(data['description'])
            self.assertIn(name, config.target_label(name, data))

    def test_bad_port_schema_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            for extra in ({'port': 42}, {'port_globs': 'not-a-list'}, {'display_id': 42},
                          {'display_id': 'NINECHARS'}, {'display_id': 'bad space'}):
                (folder / 'example.json').write_text(json.dumps({
                    'method': 'arduino-cli', 'profile': 'example', **extra}))
                with self.assertRaises(RuntimeError): config.load_target('example', folder)

    def test_eight_character_profile_variants(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            for short in ('ES3_5A', 'ES3_35AB', 'RP2_2B'):
                (folder / 'example.json').write_text(json.dumps({
                    'method': 'arduino-cli', 'profile': 'example', 'display_id': short}))
                _, data = config.load_target('example', folder)
                self.assertEqual(data['display_id'], short)


if __name__ == '__main__':
    unittest.main()
