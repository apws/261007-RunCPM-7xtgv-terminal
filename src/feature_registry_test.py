#!/usr/bin/env python3

from pathlib import Path
import re
import tempfile
import unittest

import feature_registry as registry


class FeatureRegistryTests(unittest.TestCase):
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
            path = Path(directory) / "FEATURES.md"
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
        self.assertIn("| 260922 | Test | **I** | Generated row |", result)


if __name__ == "__main__":
    unittest.main()
