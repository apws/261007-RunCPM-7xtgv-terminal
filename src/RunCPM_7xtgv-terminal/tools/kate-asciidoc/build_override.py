#!/usr/bin/env python3
"""Generate a narrow AsciiDoc/INI override from the installed KDE grammar.

Output is deliberately exclusive: never replace an existing customization.
The companion .base is formatting-normalized upstream XML; .patch records
only the semantic changes against it. Neither is loaded as a syntax file.
"""

import argparse
import copy
import difflib
from pathlib import Path
import xml.etree.ElementTree as ET


DEFAULT_SOURCE = Path(
    "/usr/share/org.kde.syntax-highlighting/syntax-bundled/asciidoc.xml"
)


def serialize(root):
    ET.indent(root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE language>\n' + ET.tostring(
        root, encoding="unicode"
    ) + "\n"


def generate(source):
    root = ET.parse(source).getroot()
    if root.get("name") != "AsciiDoc":
        raise ValueError("Expected the KDE AsciiDoc definition")
    base = serialize(copy.deepcopy(root))
    contexts = root.find("./highlighting/contexts")
    dispatch = contexts.find("context[@name='R block without title']")
    verbatim = contexts.find("context[@name='verbatim']")
    styles = {item.get("name") for item in root.findall("./highlighting/itemDatas/itemData")}
    if dispatch is None or verbatim is None or not {"Verbatim", "Delimiter", "Preprocessor"} <= styles:
        raise ValueError("Upstream grammar changed: review integration before generating")
    if "##INI Files" in base:
        raise ValueError("Upstream already embeds INI: review before applying this extension")

    root.set("version", str(int(root.get("version")) + 1))
    dispatch.insert(0, ET.Comment(
        " Local 260923: recognize INI before generic source; all other languages unchanged. "
    ))
    dispatch.insert(1, ET.Element("RegExpr", {
        "String": r"^\[source\s*,\s*ini\s*(?:,.*)?\]\s*$",
        "attribute": "Preprocessor", "context": "local INI pending", "column": "0",
    }))

    contexts.append(ET.Comment(
        " Local 260923: reuse stock verbatim fallback for unsupported or absent delimiters. "
    ))
    pending = ET.SubElement(contexts, "context", {
        "name": "local INI pending", "attribute": "Verbatim",
    })
    ET.SubElement(pending, "RegExpr", {
        "String": r"^(-{4,})\s*$", "attribute": "Delimiter",
        "context": "#pop!local INI delimited", "beginRegion": "block", "column": "0",
    })
    ET.SubElement(pending, "IncludeRules", {"context": "verbatim"})

    contexts.append(ET.Comment(
        " Local 260923: exact captured delimiter first; INI child contexts end at EOL. "
    ))
    embedded = ET.SubElement(contexts, "context", {
        "name": "local INI delimited", "attribute": "Verbatim",
    })
    ET.SubElement(embedded, "RegExpr", {
        "String": r"^%1\s*$", "attribute": "Delimiter", "context": "#pop",
        "endRegion": "block", "column": "0", "dynamic": "1",
    })
    ET.SubElement(embedded, "IncludeRules", {
        "context": "##INI Files", "includeAttrib": "true",
    })
    result = serialize(root)
    patch = "".join(difflib.unified_diff(
        base.splitlines(True), result.splitlines(True),
        fromfile="asciidoc.xml.base", tofile="asciidoc.xml",
    ))
    return base, result, patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    base, result, patch = generate(args.source)
    outputs = {
        "asciidoc.xml.base": base,
        "asciidoc.xml.patch": patch,
        "asciidoc.xml": result,
    }
    for name in outputs:
        if (args.output_dir / name).exists():
            parser.error(f"Refusing to overwrite {args.output_dir / name}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        with (args.output_dir / name).open("x", encoding="utf-8") as stream:
            stream.write(content)
    print(f"Generated {args.output_dir / 'asciidoc.xml'} from {args.source}")


if __name__ == "__main__":
    main()
