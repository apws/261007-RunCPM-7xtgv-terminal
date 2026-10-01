import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RunCPMCoreHooksTests(unittest.TestCase):
    def test_cpu_loops_use_optional_abort_hook(self):
        for number in range(1, 5):
            with self.subTest(cpu=number):
                source = (ROOT / f'cpu{number}.h').read_text()
                self.assertIn('#ifdef RUNCPM_ABORT_REQUESTED', source)
                self.assertIn('RUNCPM_ABORT_REQUESTED()', source)
                self.assertNotIn('forcedWarmReturn()', source)

    def test_console_and_disk_hooks_are_product_neutral(self):
        console = (ROOT / 'cpm.h').read_text()
        disk = (ROOT / 'disk.h').read_text()
        self.assertEqual(console.count('if (RUNCPM_ABORT_REQUESTED())'), 3)
        self.assertNotIn('forcedWarmReturn()', console)
        self.assertIn('RUNCPM_MODIFIED_FILE_CLOSED((const char *)filename);', disk)
        self.assertNotIn('terminalConfigFileClosed', disk)

    def test_cpu_attributes_are_target_supplied(self):
        cpu = (ROOT / 'cpu1.h').read_text()
        hooks = (ROOT / 'hardware/arduino_7xtgv/runcpm_hooks.h').read_text()
        board = (ROOT / 'hardware/arduino_7xtgv/boards/jc8048w535.h').read_text()
        self.assertIn('#ifdef RUNCPM_Z80RUN_ATTRIBUTES', cpu)
        self.assertNotIn('RUNCPM_TARGET_JC8048W535', cpu)
        self.assertIn('IRAM_ATTR __attribute__((optimize("O2")))', hooks)
        self.assertIn('#define XTGV_CPU1_IRAM_O2 1', board)


if __name__ == '__main__':
    unittest.main()
