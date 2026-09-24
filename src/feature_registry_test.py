#!/usr/bin/env python3

from pathlib import Path
from html.parser import HTMLParser
import re
import shutil
import subprocess
import tempfile
import unittest

import feature_registry as registry


class TableContents(HTMLParser):
    def __init__(self):
        super().__init__()
        self.cells = []
        self.links = []
        self.current = None

    def handle_starttag(self, tag, attrs):
        if tag == "td":
            self.current = []
        if tag == "a":
            self.links.append(dict(attrs).get("href"))

    def handle_data(self, data):
        if self.current is not None:
            self.current.append(data)

    def handle_endtag(self, tag):
        if tag == "td":
            self.cells.append("".join(self.current).strip())
            self.current = None


class FeatureRegistryTests(unittest.TestCase):
    def render_html(self, source):
        if not shutil.which("asciidoctor"):
            self.skipTest("optional Asciidoctor integration check")
        result = subprocess.run(
            ["asciidoctor", "-s", "-o", "-", "-"], input=source,
            text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stderr, "")
        parsed = TableContents()
        parsed.feed(result.stdout)
        return parsed

    def test_canonical_registry_validates(self) -> None:
        records = registry.read_registry(registry.DEFAULT_STORE)
        self.assertEqual([], registry.validate(records, registry.DEFAULT_STORE))
        self.assertGreaterEqual(len(records), 100)

    def test_generated_ledger_has_record_parity_and_is_current(self) -> None:
        records = registry.read_registry(registry.DEFAULT_STORE)
        region = registry.render_ledger_region(records)
        rows = [
            line for line in region.splitlines()
            if re.match(r"^\| (?:-|[0-9]{6}) \|", line)
        ]
        self.assertEqual(len(records), len(rows))
        expected = registry.update_ledger(
            registry.DEFAULT_LEDGER, records, dry_run=True
        )
        self.assertEqual(
            registry.DEFAULT_LEDGER.read_text(encoding="utf-8"), expected
        )

    def test_group_and_date_sort_puts_unknown_dates_last(self) -> None:
        records = [
            {field: "" for field in registry.FIELDS},
            {field: "" for field in registry.FIELDS},
            {field: "" for field in registry.FIELDS},
        ]
        records[0].update(group="DEV", idea_date="", title="Unknown")
        records[1].update(group="APP", idea_date="260920", title="Later group")
        records[2].update(group="DEV", idea_date="260910", title="Known")
        result = registry.sort_records(records, ["group", "idea_date", "title"])
        self.assertEqual(["Known", "Unknown", "Later group"], [r["title"] for r in result])

    def test_selected_markdown_escapes_pipe(self) -> None:
        record = {field: "" for field in registry.FIELDS}
        record.update(group="DEV", title="A | B")
        report = registry.render_markdown([record], ["group", "title"])
        self.assertIn("A \\| B", report)

    def test_filters_accept_equal_and_not_equal(self) -> None:
        records = [
            {**{field: "" for field in registry.FIELDS}, "group": "DEV"},
            {**{field: "" for field in registry.FIELDS}, "group": "INP"},
        ]
        self.assertEqual(1, len(registry.apply_filters(records, ["group=DEV"])))
        self.assertEqual(1, len(registry.apply_filters(records, ["group!=DEV"])))

    def test_ledger_update_preserves_hand_written_regions(self) -> None:
        record = {field: "" for field in registry.FIELDS}
        record.update(
            feature_id="dev-test",
            group="DEV",
            title="Test",
            state="I",
            idea_date="260922",
            last_reviewed="260922",
            summary="Generated row",
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "FEATURES.adoc"
            path.write_text(
                "before\n"
                + registry.BEGIN_MARKER
                + "\nold\n"
                + registry.END_MARKER
                + "\nafter\n",
                encoding="utf-8",
            )
            registry.update_ledger(path, [record], dry_run=False)
            result = path.read_text(encoding="utf-8")
        self.assertTrue(result.startswith("before\n"))
        self.assertTrue(result.endswith("\nafter\n"))
        self.assertIn("| 260922 | pass:[Test] | *I* | pass:[Generated row] |", result)

    def test_asciidoc_reports_preserve_literal_data(self) -> None:
        values = [
            "A | B", "*bold* _italic_ `code` {attribute} [label]",
            r"send:\^K\^D and \x81", "<script>& \"quoted\"",
            "pass:[literal] ++plus++ ^caret^ ~tilde~ #hash#",
            "https://example.com (C) -- ... ->", "&#124; &#93; &amp;", "",
        ]
        records = [{"title": value, "state": "D"} for value in values]
        report = registry.render_asciidoc(records, ["title", "state"])
        rendered = self.render_html(report)
        self.assertEqual(rendered.cells, [cell for value in values for cell in (value, "D")])
        self.assertEqual(rendered.links, [])

    def test_asciidoc_ledger_links_and_all_rows_render(self) -> None:
        records = registry.read_registry(registry.DEFAULT_STORE)
        rendered = self.render_html(registry.render_ledger_region(records))
        expected = registry.sort_records(records, ["group", "idea_date", "title"])
        self.assertEqual(len(rendered.cells), 5 * len(records))
        self.assertEqual(rendered.cells[1::5], [record["title"] for record in expected])
        self.assertEqual(len(rendered.links), sum(len(list(filter(None, r["docs"].split(";")))) for r in records))
        for href in rendered.links:
            self.assertNotIn(" ", href)
        # Macro-looking punctuation in filenames must not corrupt labels/targets.
        link = registry.doc_link("docs/a [b]|{c} & _d_.adoc")
        rendered = self.render_html('[cols="1"]\n|===\n| ' + link + '\n|===\n')
        self.assertEqual(rendered.cells, ["a [b]|{c} & _d_.adoc"])
        self.assertEqual(rendered.links, ["a%20%5Bb%5D%7C%7Bc%7D%20%26%20_d_.adoc"])

    def test_empty_asciidoc_report(self) -> None:
        rendered = self.render_html(registry.render_asciidoc([], ["title", "state"]))
        self.assertEqual(rendered.cells, [])

    def test_asciidoc_cli_aliases(self) -> None:
        parser = registry.build_parser()
        for name in ("asciidoc", "adoc", "markdown", "tsv", "text"):
            args = parser.parse_args(["report", "--format", name])
            self.assertEqual(args.format, name)

    def test_ledger_markers_out_of_order_report_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "FEATURES.adoc"
            path.write_text(registry.END_MARKER + "\n" + registry.BEGIN_MARKER)
            with self.assertRaisesRegex(registry.RegistryError, "out of order"):
                registry.update_ledger(path, [], dry_run=False)


if __name__ == "__main__":
    unittest.main()
