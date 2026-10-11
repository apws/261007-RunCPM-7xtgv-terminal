"""Keep host OFF mappings distinct from ANSI/VT52 selection."""
from pathlib import Path
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"


class BacklightShortcutTests(unittest.TestCase):
    def test_off_requires_alt_in_both_hosts(self):
        konsole = (TOOLS / "7xtgv-terminal.keytab").read_text()
        kitty = (TOOLS / "kitty-cpm.conf").read_text()
        self.assertIn(r'key 0         +Shift+Alt+Ctrl : "\E[>63;0v"', konsole)
        self.assertNotIn(r'key 0         +Shift-Alt+Ctrl : "\E[>63;0v"', konsole)
        self.assertIn(r'map ctrl+alt+shift+0 send_text all \x1b[>63;0v', kitty)
        self.assertNotIn(r'map ctrl+shift+0 send_text all \x1b[>63;0v', kitty)
        kitty_default = (TOOLS / "kitty.conf").read_text()
        self.assertIn(r'map ctrl+alt+shift+0 send_text all \x1b[>63;0v', kitty_default)
        self.assertNotIn(r'map ctrl+shift+0 send_text all \x1b[>63;0v', kitty_default)
        self.assertIn(r'key 1         +Shift+Alt+Ctrl : "\E[>1v"', konsole)
        self.assertIn(r'key 2         +Shift+Alt+Ctrl : "\E[>2v"', konsole)


if __name__ == "__main__":
    unittest.main()
