#!/usr/bin/env python3
"""Validate and report the canonical 7xtgv feature registry."""

from __future__ import annotations

import argparse
import html
import os
from pathlib import Path
import re
import sys
import tempfile
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_STORE = ROOT / "docs" / "features.tsv"
DEFAULT_LEDGER = ROOT / "docs" / "FEATURES.adoc"

FIELDS = (
    "feature_id",
    "group",
    "title",
    "state",
    "qualifier",
    "idea_date",
    "design_date",
    "plan_date",
    "impl_date",
    "reject_date",
    "hw_test_date",
    "docs",
    "summary",
    "boundary",
    "next_gate",
    "last_reviewed",
)

GROUPS = {
    "RUN": "RunCPM, targets and host tooling",
    "OUT": "Terminal output, display and graphics",
    "INP": "Input, profiles, history and smart-terminal work",
    "CFG": "Configuration, diagnostics and operational safety",
    "NAT": "Native 7xtgv architecture",
    "DEV": "Development tools and process features",
    "APP": "Application and historical compatibility investigations",
}
GROUP_RANK = {name: rank for rank, name in enumerate(GROUPS)}
STATES = frozenset(("B", "D", "P", "I", "H", "X", "R"))
DATE_FIELDS = (
    "idea_date",
    "design_date",
    "plan_date",
    "impl_date",
    "reject_date",
    "hw_test_date",
    "last_reviewed",
)

BEGIN_MARKER = "// feature-registry:begin"
END_MARKER = "// feature-registry:end"
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
DATE_RE = re.compile(r"(?:|[0-9]{6})\Z")


class RegistryError(Exception):
    pass


def read_registry(path: Path) -> list[dict[str, str]]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise RegistryError(f"cannot read {path}: {exc}") from exc

    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise RegistryError(
            f"{path}:{exc.start}: canonical registry is not 7-bit ASCII"
        ) from exc

    if b"\r" in raw:
        raise RegistryError(f"{path}: CR bytes are forbidden; use LF records")

    lines = text.splitlines()
    if not lines:
        raise RegistryError(f"{path}: empty registry")
    if tuple(lines[0].split("\t")) != FIELDS:
        raise RegistryError(
            f"{path}: header differs from the required {len(FIELDS)}-field schema"
        )

    records: list[dict[str, str]] = []
    for line_number, line in enumerate(lines[1:], 2):
        if not line:
            raise RegistryError(f"{path}:{line_number}: blank physical record")
        values = line.split("\t")
        if len(values) != len(FIELDS):
            raise RegistryError(
                f"{path}:{line_number}: expected {len(FIELDS)} fields, "
                f"found {len(values)}"
            )
        records.append(dict(zip(FIELDS, values)))
    return records


def validate(records: list[dict[str, str]], store: Path) -> list[str]:
    errors: list[str] = []
    seen: set[str] = set()
    root = ROOT.resolve()

    for index, record in enumerate(records, 2):
        prefix = f"{store}:{index}"
        feature_id = record["feature_id"]
        if not ID_RE.fullmatch(feature_id):
            errors.append(f"{prefix}: invalid feature_id {feature_id!r}")
        elif feature_id in seen:
            errors.append(f"{prefix}: duplicate feature_id {feature_id!r}")
        seen.add(feature_id)

        if record["group"] not in GROUPS:
            errors.append(f"{prefix}: unknown group {record['group']!r}")
        if record["state"] not in STATES:
            errors.append(f"{prefix}: unknown state {record['state']!r}")
        if not record["title"]:
            errors.append(f"{prefix}: empty title")

        for field in DATE_FIELDS:
            value = record[field]
            if not DATE_RE.fullmatch(value):
                errors.append(f"{prefix}: {field} is not empty or YYMMDD: {value!r}")

        for name, value in record.items():
            if any(char in value for char in "\t\r\n"):
                errors.append(f"{prefix}: {name} contains a record delimiter")

        for relative in filter(None, record["docs"].split(";")):
            candidate = Path(relative)
            if candidate.is_absolute():
                errors.append(f"{prefix}: absolute document path {relative!r}")
                continue
            resolved = (ROOT / candidate).resolve()
            try:
                resolved.relative_to(root)
            except ValueError:
                errors.append(f"{prefix}: document escapes repository: {relative!r}")
                continue
            if not resolved.is_file():
                errors.append(f"{prefix}: missing document {relative!r}")

    return errors


def parse_fields(spec: str) -> list[str]:
    fields = [field.strip() for field in spec.split(",") if field.strip()]
    unknown = [field for field in fields if field not in FIELDS]
    if unknown:
        raise RegistryError(f"unknown field(s): {', '.join(unknown)}")
    if not fields:
        raise RegistryError("field list is empty")
    return fields


def apply_filters(
    records: list[dict[str, str]], clauses: list[str]
) -> list[dict[str, str]]:
    parsed: list[tuple[str, str, bool]] = []
    for clause in clauses:
        if "!=" in clause:
            field, value = clause.split("!=", 1)
            negate = True
        elif "=" in clause:
            field, value = clause.split("=", 1)
            negate = False
        else:
            raise RegistryError(f"filter must use FIELD=VALUE or FIELD!=VALUE: {clause}")
        field = field.strip()
        if field not in FIELDS:
            raise RegistryError(f"unknown filter field: {field}")
        parsed.append((field, value, negate))

    result = records
    for field, value, negate in parsed:
        if negate:
            result = [record for record in result if record[field] != value]
        else:
            result = [record for record in result if record[field] == value]
    return result


def sort_records(
    records: list[dict[str, str]], fields: list[str]
) -> list[dict[str, str]]:
    def value(record: dict[str, str], field: str) -> tuple[object, ...]:
        current = record[field]
        if field == "group":
            return (False, GROUP_RANK.get(current, len(GROUP_RANK)))
        return (current == "", current.casefold())

    return sorted(records, key=lambda record: tuple(value(record, f) for f in fields))


def md_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("|", "\\|")


def render_markdown(records: list[dict[str, str]], fields: list[str]) -> str:
    labels = [field.replace("_", " ").title() for field in fields]
    lines = [
        "| " + " | ".join(labels) + " |",
        "| " + " | ".join("---" for _ in fields) + " |",
    ]
    for record in records:
        lines.append(
            "| " + " | ".join(md_escape(record[field]) for field in fields) + " |"
        )
    return "\n".join(lines) + "\n"


def adoc_escape(value: str) -> str:
    """Literal inline data, safe even inside a table cell or link label.

    Escape HTML first, then protect the table separator and macro terminator.
    The passthrough prevents data from becoming attributes, links or emphasis.
    """
    if not value:
        return ""
    escaped = html.escape(value, quote=False).replace("|", "&#124;").replace("]", "&#93;")
    return f"pass:[{escaped}]"


def render_asciidoc(records: list[dict[str, str]], fields: list[str]) -> str:
    lines = [f'[cols="{",".join("1" for _ in fields)}",options="header"]', "|==="]
    lines.append("| " + " | ".join(field.replace("_", " ").title() for field in fields))
    lines.append("")
    for record in records:
        lines.append("| " + " | ".join(adoc_escape(record[field]) for field in fields))
    lines.append("|===")
    return "\n".join(lines) + "\n"


def render_tsv(records: list[dict[str, str]], fields: list[str]) -> str:
    lines = ["\t".join(fields)]
    lines.extend("\t".join(record[field] for field in fields) for record in records)
    return "\n".join(lines) + "\n"


def render_text(records: list[dict[str, str]], fields: list[str]) -> str:
    labels = [field.replace("_", " ").upper() for field in fields]
    widths = [len(label) for label in labels]
    for record in records:
        for index, field in enumerate(fields):
            widths[index] = max(widths[index], len(record[field]))

    def row(values: list[str]) -> str:
        return "  ".join(value.ljust(width) for value, width in zip(values, widths)).rstrip()

    lines = [row(labels), row(["-" * width for width in widths])]
    lines.extend(row([record[field] for field in fields]) for record in records)
    return "\n".join(lines) + "\n"


def doc_link(relative: str) -> str:
    path = Path(relative)
    if path.parts and path.parts[0] == "docs":
        target = Path(*path.parts[1:]).as_posix()
    else:
        target = "../" + path.as_posix()
    return f"link:{quote(target, safe='/._-')}[{adoc_escape(path.name)}]"


def ledger_detail(record: dict[str, str]) -> str:
    parts = [record["summary"]]
    if record["boundary"]:
        parts.append("Boundary: " + record["boundary"])
    if record["next_gate"]:
        parts.append("Next: " + record["next_gate"])
    return adoc_escape(" ".join(filter(None, parts)))


def render_ledger_region(records: list[dict[str, str]]) -> str:
    records = sort_records(records, ["group", "idea_date", "title"])
    lines = [
        "Generated from `docs/features.tsv` by `src/feature_registry.py`; "
        "do not edit these tables directly.",
        "",
    ]
    for group, heading in GROUPS.items():
        lines.extend(
            [
                f"== {heading}",
                "",
                '[cols="1,3,1,5,3",options="header"]',
                "|===",
                "| Introduced | Feature | State | Summary and remaining boundary | Documents",
                "",
            ]
        )
        for record in (item for item in records if item["group"] == group):
            state = f"*{record['state']}*"
            if record["qualifier"]:
                state += ", " + adoc_escape(record["qualifier"])
            documents = ", ".join(
                doc_link(path) for path in filter(None, record["docs"].split(";"))
            )
            lines.append(
                "| "
                + " | ".join(
                    (
                        record["idea_date"] or "-",
                        adoc_escape(record["title"]),
                        state,
                        ledger_detail(record),
                        documents,
                    )
                )
            )
        lines.extend(["|===", ""])
    return "\n".join(line.rstrip() for line in lines).rstrip() + "\n"


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = path.stat().st_mode if path.exists() else 0o644
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", dir=path.parent, delete=False
    ) as stream:
        temporary = Path(stream.name)
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.chmod(mode)
    temporary.replace(path)


def update_ledger(path: Path, records: list[dict[str, str]], dry_run: bool) -> str:
    try:
        current = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RegistryError(f"cannot read {path}: {exc}") from exc
    if current.count(BEGIN_MARKER) != 1 or current.count(END_MARKER) != 1:
        raise RegistryError(f"{path}: generated-region markers missing or duplicated")
    if current.index(END_MARKER) < current.index(BEGIN_MARKER):
        raise RegistryError(f"{path}: generated-region markers out of order")
    before, tail = current.split(BEGIN_MARKER, 1)
    _, after = tail.split(END_MARKER, 1)
    region = render_ledger_region(records)
    updated = before + BEGIN_MARKER + "\n\n" + region + "\n" + END_MARKER + after
    if not dry_run and updated != current:
        atomic_write(path, updated)
    return updated


def write_output(text: str, destination: str) -> None:
    if destination == "-":
        sys.stdout.write(text)
    else:
        atomic_write(Path(destination), text)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--store", type=Path, default=DEFAULT_STORE)
    parser.add_argument("--ledger", type=Path, default=DEFAULT_LEDGER)
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("check", help="validate canonical TSV and document links")

    report = commands.add_parser("report", help="emit a selected and sorted report")
    report.add_argument(
        "--fields",
        default="group,idea_date,title,state,qualifier,impl_date,hw_test_date,next_gate",
        help="comma-separated output fields",
    )
    report.add_argument(
        "--sort", default="group,idea_date,title", help="comma-separated sort fields"
    )
    report.add_argument(
        "--where", action="append", default=[], metavar="FIELD=VALUE",
        help="exact filter; FIELD!=VALUE is also accepted",
    )
    report.add_argument("--format", choices=("asciidoc", "adoc", "markdown", "text", "tsv"), default="text")
    report.add_argument("--output", "-o", default="-", help="output path or - for stdout")

    update = commands.add_parser(
        "update-ledger", help="replace the marked generated tables in FEATURES.adoc"
    )
    update.add_argument("--dry-run", action="store_true", help="print without writing")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        records = read_registry(args.store)
        errors = validate(records, args.store)
        if errors:
            for error in errors:
                print(error, file=sys.stderr)
            return 1

        if args.command == "check":
            print(f"OK: {len(records)} features in {args.store}")
            return 0

        if args.command == "report":
            fields = parse_fields(args.fields)
            sort_fields = parse_fields(args.sort)
            selected = sort_records(apply_filters(records, args.where), sort_fields)
            renderer = {
                "asciidoc": render_asciidoc,
                "adoc": render_asciidoc,
                "markdown": render_markdown,
                "text": render_text,
                "tsv": render_tsv,
            }[args.format]
            write_output(renderer(selected, fields), args.output)
            return 0

        if args.command == "update-ledger":
            updated = update_ledger(args.ledger, records, args.dry_run)
            if args.dry_run:
                sys.stdout.write(updated)
            else:
                print(f"Updated {args.ledger} from {len(records)} feature records")
            return 0
    except RegistryError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
