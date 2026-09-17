import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from history_to_tokens import entries


class HistoryTokensTest(unittest.TestCase):
    def test_v2_crc_and_bank_order(self):
        body = (
            b"# 7xtgv terminal history v2\n[0]\n>CLS\n[5]\n>for\n>abs(\n"
            b"[1]\n>LIST\n"
        )
        saved = body + f"# crc32={zlib.crc32(body):08X}\n".encode() + b"\0" * 17
        self.assertEqual(entries(saved, 5), [b"for", b"abs("])
        with self.assertRaisesRegex(ValueError, "CRC mismatch"):
            entries(saved.replace(b">for", b">FOR"), 5)

    def test_v1(self):
        saved = b"# 7xtgv terminal history v1\n[5]\n>PRINT\n>print\n[6]\n>X\n"
        self.assertEqual(entries(saved, 5), [b"PRINT", b"print"])


if __name__ == "__main__":
    unittest.main()
