"""Execute the P4 reboot function with ordered peripheral test doubles."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class P4RebootTests(unittest.TestCase):
    def test_blank_before_restart_only_with_terminal(self):
        board = (ROOT / 'hardware/arduino_7xtgv/boards/jc8012p4a1c.h').read_text()
        reboot = board[board.index('void runCPMHardwareReboot()'):board.rindex('#endif // XTGV_HAL_IMPLEMENTATION')]
        source = r'''
#include <cassert>
constexpr int XTGV_BACKLIGHT_PIN = 23, OUTPUT = 1, LOW = 0;
static int step = 0;
bool ledcWrite(int pin, int duty) {
    assert(pin == 23 && duty == 0 && step++ == 0); return true;
}
bool ledcDetach(int pin) {
    assert(pin == 23 && step++ == 1); return true;
}
void pinMode(int pin, int mode) {
    assert(pin == 23 && mode == OUTPUT && step++ == 2);
}
void digitalWrite(int pin, int level) {
    assert(pin == 23 && level == LOW && step++ == 3);
}
struct Esp {
    void restart() { assert(step == (XTGV_TERMINAL ? 4 : 0)); ++step; }
} ESP;
''' + reboot + r'''
int main() {
    runCPMHardwareReboot();
    assert(step == (XTGV_TERMINAL ? 5 : 1));
}
'''
        with tempfile.TemporaryDirectory(prefix='xtgv-p4-reboot-') as temp:
            for terminal in (0, 1):
                with self.subTest(terminal=terminal):
                    binary = Path(temp) / f'reboot-{terminal}'
                    result = subprocess.run(
                        ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                         '-DARDUINO', f'-DXTGV_TERMINAL={terminal}',
                         '-x', 'c++', '-', '-o', str(binary)],
                        input=source, text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
