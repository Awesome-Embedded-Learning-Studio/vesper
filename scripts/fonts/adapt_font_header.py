#!/usr/bin/env python3
"""Adapts a visor fontgen header into this repo's house style.

fontgen's output is visor-shaped: raw C arrays, per-glyph comments, the
visor::assets namespace. The repo keeps std::array tables (one explicit
inner brace, so GCC reads the designators as std::array's member rather
than a GNU array designator), no per-glyph comments in the glyph table
(they wrap and fight the trailing-comma check), a namespace under
vesper::fonts, and a MonoFont view at the tail.

Usage:
    adapt_font_header.py <fontgen-header.hpp> <final.hpp> \
        --namespace vesper::fonts::ui --view kUiFont
"""

import argparse
import re
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", help="raw header written by fontgen")
    parser.add_argument("output", help="adapted header path")
    parser.add_argument(
        "--namespace",
        required=True,
        help="target namespace, e.g. vesper::fonts::ui",
    )
    parser.add_argument(
        "--view",
        required=True,
        help="name of the MonoFont view to append, e.g. kUiFont",
    )
    args = parser.parse_args()

    text = open(args.source, encoding="utf-8").read()
    source_namespace = re.search(r"namespace ([\w:]+) \{", text)
    if source_namespace is None:
        sys.exit("adapter: no source namespace in the fontgen header")
    tail_namespace = re.search(r"} // namespace [\w:]+", text)
    if tail_namespace is None:
        sys.exit("adapter: no closing namespace comment in the fontgen header")

    bits_match = re.search(r"inline constexpr std::uint8_t kBits\[\] = \{\n(.*?)\n\};", text, re.S)
    glyph_match = re.search(
        r"inline constexpr visor::draw::GlyphEntry kGlyphs\[\] = \{\n(.*?)\n\};", text, re.S
    )
    if bits_match is None or glyph_match is None:
        sys.exit("adapter: could not find the generated tables")

    bits_lines = [
        line.rstrip() + ("," if line.rstrip().endswith(",") else ",")
        for line in bits_match.group(1).splitlines()
    ]
    # One row may pack several bytes onto one line when its glyph is wider
    # than eight pixels, so the element count is the byte count, not the
    # line count.
    bits_count = len(re.findall(r"0x[0-9a-fA-F]{2}", bits_match.group(1)))

    glyph_lines = []
    for line in glyph_match.group(1).splitlines():
        entry = line.split("//")[0].strip()
        if entry.startswith("{.codepoint"):
            # The trailing comma before the closing brace: clang-format wraps
            # these entries onto several lines, and a multi-line initializer
            # without one fails the trailing-comma check.
            glyph_lines.append(entry.rstrip(",").rstrip("}") + ",},")
    glyph_count = len(glyph_lines)

    bits_block = (
        f"inline constexpr std::array<std::uint8_t, {bits_count}> kBits{{\n"
        + "\n".join(bits_lines)
        + "\n};"
    )
    glyph_block = (
        f"inline constexpr std::array<visor::draw::GlyphEntry, {glyph_count}> kGlyphs{{\n"
        "    {\n" + "\n".join(glyph_lines) + "\n    },\n};"
    )
    text = text[: bits_match.start()] + bits_block + "\n\n" + glyph_block + text[glyph_match.end() :]
    text = text.replace(
        '#include "visor/draw/mono_font.hpp"',
        '#include <array>\n\n#include "visor/draw/mono_font.hpp"',
    )
    text = text.replace(
        f"namespace {source_namespace.group(1)} {{", f"namespace {args.namespace} {{"
    )
    text = text.replace(
        tail_namespace.group(0),
        f"""inline constexpr visor::draw::MonoFont {args.view}{{
    .line_height = kLineHeight,
    .glyphs = kGlyphs,
    .bits = kBits,
}};

}} // namespace {args.namespace}""",
    )
    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write(text)
    print(
        f"{args.output}: {glyph_count} glyphs in {args.namespace}, view {args.view}"
    )


if __name__ == "__main__":
    main()
