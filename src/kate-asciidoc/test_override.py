#!/usr/bin/env python3
"""Exercise the real KDE highlighter with isolated user-data directories."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from build_override import generate, DEFAULT_SOURCE


class EmbeddedIniTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="xtgv-kate-test-")
        cls.root = Path(cls.temp.name)
        cls.base_env = dict(os.environ, XDG_DATA_HOME=str(cls.root / "stock"))
        cls.env = dict(os.environ, XDG_DATA_HOME=str(cls.root / "patched"))
        syntax = cls.root / "patched/org.kde.syntax-highlighting/syntax"
        syntax.mkdir(parents=True)
        _, xml, _ = generate(DEFAULT_SOURCE)
        (syntax / "asciidoc.xml").write_text(xml, encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def highlight(self, source, syntax="AsciiDoc", patched=True):
        process = subprocess.run(
            ["ksyntaxhighlighter6", "--syntax", syntax, "--theme", "Breeze Light"],
            input=source, text=True, capture_output=True,
            env=self.env if patched else self.base_env, check=True,
        )
        self.assertEqual(process.stderr, "", "KDE grammar warning")
        return process.stdout.split("<pre>\n", 1)[1].split("</pre>", 1)[0].splitlines()

    def test_ini_uses_existing_language_styles(self):
        lines = ["[palette0]", "", "; comment", "# another comment", "paper0 = RGB565",
                 "ink00=123", "enabled=on", "[palette1]", r"escape=send:\^K\^D"]
        result = self.highlight("[source,ini]\n----\n" + "\n".join(lines) + "\n----\n")
        self.assertEqual(result[2:-1], self.highlight("\n".join(lines) + "\n", "INI Files"))

    def test_attribute_spacing_and_options(self):
        expected = self.highlight("key=value\n", "INI Files")[0]
        for attribute in ("[source,ini]", "[source, ini]", "[source,ini,linenums]", "[source, ini ]"):
            with self.subTest(attribute=attribute):
                self.assertEqual(self.highlight(attribute + "\n----\nkey=value\n----\n")[2], expected)

    def test_delimiter_length_and_prefix(self):
        text = "[source,ini]\n------\n----\n----content\n------content\nkey=value\n------  \n\n== Back\n"
        result = self.highlight(text)
        self.assertEqual(result[2:6], self.highlight("----\n----content\n------content\nkey=value\n", "INI Files"))
        self.assertEqual(result[-1], self.highlight("== Back\n", patched=False)[0])

    def test_normal_text_and_second_block(self):
        text = "= Test\n\n[source,ini]\n----\n; comment\n----\n\nNormal *bold*.\n\n[source,ini]\n----\nk=v\n----\n"
        result = self.highlight(text)
        self.assertEqual(result[7], self.highlight("Normal *bold*.\n", patched=False)[0])
        self.assertEqual(result[11], self.highlight("k=v\n", "INI Files")[0])

    def test_other_blocks_unchanged(self):
        for text in (
            "[source,cpp]\n----\nint x;\n----\n\nText\n",
            "----\n[source,ini]\n[section]\nk=v\n----\n",
            "....\n[source,ini]\n----\nk=v\n----\n....\n",
            "[source,initial]\n----\nk=v\n----\n",
            "[source,ini]\n....\nk=v\n....\n\nText\n",
            "[source,ini]\nnot a delimiter\n\n== Back\nText\n",
            "[source,ini]\n\n== Back\nText\n",
            "// [source,ini]\n----\nk=v\n----\n",
        ):
            with self.subTest(text=text):
                self.assertEqual(self.highlight(text), self.highlight(text, patched=False))

    def test_nested_example_and_list(self):
        for prefix, suffix in (("====\n", "====\n"), ("* Item\n+\n", "\n* Next\n")):
            text = prefix + "[source,ini]\n----\nk=v\n----\n" + suffix
            self.assertIn(self.highlight("k=v\n", "INI Files")[0], self.highlight(text))

    def test_generator_refuses_existing_override(self):
        destination = self.root / "no-clobber"
        destination.mkdir()
        existing = destination / "asciidoc.xml"
        existing.write_text("Existing user customization", encoding="utf-8")
        process = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("build_override.py")),
             "--output-dir", str(destination)], text=True, capture_output=True,
        )
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("Refusing to overwrite", process.stderr)
        self.assertEqual(existing.read_text(), "Existing user customization")
        self.assertEqual(list(destination.iterdir()), [existing])


if __name__ == "__main__":
    unittest.main()
