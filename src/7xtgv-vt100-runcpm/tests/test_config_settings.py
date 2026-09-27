"""Compile the actual settings edits against the owned in-memory INI text."""
from pathlib import Path
import subprocess
import tempfile
import unittest

SKETCH = Path(__file__).resolve().parents[1] / "7xtgv-vt100-runcpm.ino"


class ConfigSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="xtgv-config-test-")
        cls.binary = Path(cls.directory.name) / "writer"
        sketch = SKETCH.read_text()
        functions = sketch.split("static bool updateConfigSettings", 1)[1].split(
            "static bool writeTerminalConfigText", 1)[0]
        source = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include "src/terminal/ini_text.h"
#include "src/terminal/terminal_runtime.h"
bool failAllocation=false;
void *resize(void *p,size_t n) { return failAllocation ? nullptr : realloc(p,n); }
''' + "static bool updateConfigSettings" + functions + r'''
int main(int argc, char **argv) {
    const std::string input(std::istreambuf_iterator<char>(std::cin),{});
    vt::IniText text(resize);
    failAllocation=argc>1 && strcmp(argv[1],"fail")==0;
    uint8_t *data=text.beginLoad(input.size());
    if (!data) return 1;
    memcpy(data,input.data(),input.size());
    text.finishLoad();
    const uint8_t dirty=argc>1 && strcmp(argv[1],"backlight")==0
        ? ConfigBacklightDirty
        : ConfigBacklightDirty | ConfigPaletteDirty | ConfigZoomDirty;
    if (!updateConfigSettings(text,17,2,"rp2_2",3,dirty)) return 1;
    std::cout.write(reinterpret_cast<const char *>(text.data()),text.size());
    std::cout << char(0x1a);
}
'''
        path = Path(cls.directory.name) / "writer.cpp"
        path.write_text(source)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(SKETCH.parent), str(path), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def rewrite(self, data, mode=None):
        command = [str(self.binary)] + ([mode] if mode else [])
        return subprocess.run(command, input=data, capture_output=True, check=True).stdout

    def test_backlight_save_preserves_zoom_text(self):
        source = (b"[global]\r\nbacklight_level=16\r\nmode_palette=2\r\n"
                  b"[display:rp2_2]\r\nview_zoom = 4  ; chosen by hand\r\n")
        result = self.rewrite(source, "backlight")
        self.assertIn(b"backlight_level=17", result)
        self.assertIn(b"view_zoom = 4  ; chosen by hand\r\n", result)
        self.assertIn(b"mode_palette=2\r\n", result)

    def test_preserves_palettes_comments_and_other_displays(self):
        palette = b"[palette3]\r\n; keep me\r\npaper0=1,2,3\r\nink08=9,8,7\r\n"
        other = b"[display:ES3_5]\r\nview_zoom=4\r\n"
        data = b"[global]\r\nmode_palette=1\r\nmode_palette=9\r\nbacklight_level=16\r\ncrt_rampup_ms=1500\r\n" + palette + other
        output = self.rewrite(data + b"\x1aignored padding")
        self.assertIn(palette, output)
        self.assertIn(other, output)
        self.assertEqual(output.count(b"mode_palette="),1)
        self.assertIn(b"mode_palette=3",output)
        self.assertIn(b"backlight_level=17",output)
        self.assertIn(b"crt_rampup_ms=1500\r\n",output)
        self.assertIn(b"[display:rp2_2]\r\nview_zoom=2",output)
        self.assertTrue(output.endswith(b"\x1a"))
        self.assertNotIn(b"ignored padding",output)

    def test_missing_keys_and_idempotence(self):
        for source in (b"", b"; comment", b"[palette0]\nink00=1,2,3\n"):
            output = self.rewrite(source)
            self.assertIn(b"mode_palette=3",output)
            self.assertEqual(self.rewrite(output),output)

    def test_shared_id_case_and_repeated_sections(self):
        other = b"[display:RP2_2B]\r\nview_zoom=4\r\n"
        source = (b"[display:RP2_2]\r\nview_zoom=1\r\nzoom1_terminal=80x24\r\n" +
                  other + b"[display:rp2_2]\r\nview_zoom=3\r\nrender_flush_hz=15\r\n")
        output = self.rewrite(source)
        self.assertIn(other, output)
        self.assertEqual(output.count(b"view_zoom=2"), 1)
        self.assertNotIn(b"view_zoom=3", output)
        self.assertIn(b"zoom1_terminal=80x24", output)
        self.assertIn(b"render_flush_hz=15", output)

    def test_line_endings_and_long_lines(self):
        for ending in (b"\r",b"\n",b"\r\n"):
            long_line = b";" + b"x" * 700
            source = b"[global]"+ending+b"mode_palette=0"+ending+long_line+ending
            output = self.rewrite(source)
            self.assertIn(long_line+ending,output)
            self.assertIn(b"mode_palette=3"+ending,output)

    def test_allocation_failure(self):
        result = subprocess.run([str(self.binary),"fail"],input=b"[global]\nmode_palette=1\n",capture_output=True)
        self.assertEqual(result.returncode,1)

    def test_size_bound(self):
        result = subprocess.run([str(self.binary)],input=b'x' * 65537,capture_output=True)
        self.assertEqual(result.returncode,1)

    def test_whitespace_case_and_unterminated_last_key(self):
        output = self.rewrite(b" [GLOBAL] \n MODE_PALETTE \t=0")
        self.assertIn(b"mode_palette=3", output)
        self.assertNotIn(b"=0", output)


if __name__ == "__main__":
    unittest.main()
