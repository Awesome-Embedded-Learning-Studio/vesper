#!/usr/bin/env python3
"""Rasterizes a TrueType/OpenType face into the BDF that visor's fontgen cuts.

FreeType's monochrome renderer is used on purpose: it hints each glyph's
stems to whole pixels, which is the only way a 1bpp face stays readable at
8-12 px. Rendering the same face in grayscale and thresholding, the route
this script replaced, loses thin strokes and turns "jumps" into "jLlmps".

The BDF carries every glyph's own advance (DWIDTH) and ink offsets (BBX),
so proportional faces survive the trip: fontgen copies both into the header
and the layout cursor honours them.

Run from the repository root, then cut the header with fontgen:

    scripts/fonts/rasterize_bdf.py --font <face.ttf> --size 8 \
        --range 0x20:0x7e --output scripts/fonts/ui8.bdf
    fontgen bdf scripts/fonts/ui8.bdf <raw.hpp> --range 0x20:0x7e --name ui8
    scripts/fonts/adapt_font_header.py <raw.hpp> assets/fonts/ui_font.hpp \
        --namespace vesper::fonts --view kUiFont
"""

import argparse

from PIL import ImageFont

# The printable ASCII block every face in this repo covers. A glyph table
# with holes would paint nothing for the missing byte and measure nothing,
# so the range is fixed rather than discovered per face.
FIRST_CODEPOINT = 0x20
LAST_CODEPOINT = 0x7E


def mask_rows(mask, width, height):
    """Packs one glyph mask into MSB-first rows, (width + 7) // 8 bytes each."""
    pixels = bytes(mask)
    stride = (width + 7) // 8
    rows = []
    for y in range(height):
        row = bytearray(stride)
        for x in range(width):
            if pixels[(y * width) + x]:
                row[x // 8] |= 0x80 >> (x % 8)
        rows.append(row.hex().upper())
    return rows


def glyph_block(font, codepoint):
    char = chr(codepoint)
    mask, (offset_x, offset_y) = font.getmask2(char, mode="1")
    width, height = mask.size
    advance = max(0, round(font.getlength(char)))
    lines = [
        f"STARTCHAR char{codepoint:X}",
        f"ENCODING {codepoint}",
        f"SWIDTH {advance * 1000 // max(font.size, 1)} 0",
        f"DWIDTH {advance}",
    ]
    if width == 0 or height == 0:
        # Empty ink: the advance still moves the pen, so the glyph keeps its
        # entry with a zero box.
        lines += ["BBX 0 0 0 0", "BITMAP"]
    else:
        # BDF hangs a box off the baseline with y up; the rasterizer's offset
        # counts down from the ascender line, so the box's bottom is
        # ascent - (offset_y + height).
        ascent, _ = font.getmetrics()
        y_off = ascent - (offset_y + height)
        lines += [f"BBX {width} {height} {offset_x} {y_off}", "BITMAP"]
        lines += mask_rows(mask, width, height)
    lines.append("ENDCHAR")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, help="source TrueType/OpenType file")
    parser.add_argument("--size", type=int, required=True, help="pixel size")
    parser.add_argument(
        "--range",
        default=f"0x{FIRST_CODEPOINT:x}:0x{LAST_CODEPOINT:x}",
        help="inclusive codepoint range as first:last, hex or decimal",
    )
    parser.add_argument("--output", required=True, help="BDF path to write")
    args = parser.parse_args()

    first_text, last_text = args.range.split(":")
    first = int(first_text, 0)
    last = int(last_text, 0)
    if first > last:
        parser.error("range start is past its end")

    font = ImageFont.truetype(args.font, args.size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    # The box covers the widest advance the face carries; fontgen keeps its
    # own grid from FONT_ASCENT/FONT_DESCENT and ignores this value.
    widest = max(round(font.getlength(chr(cp))) for cp in range(first, last + 1))

    # BDF comments must be COMMENT records, and fontgen wants STARTFONT first;
    # the provenance line therefore rides inside the header, where the parser
    # skips it, rather than above STARTFONT where it would break the reader.
    lines = [
        "STARTFONT 2.1",
        f"COMMENT Rasterized by scripts/fonts/rasterize_bdf.py from {args.font} "
        f"at {args.size} px, FreeType monochrome rendering.",
        f"FONT -vesper-raster-{args.size}-{args.size}-75-75-m-{widest * 10}-"
        f"{line_height * 10}-75-75",
        f"SIZE {args.size} 75 75",
        f"FONTBOUNDINGBOX {widest} {line_height} 0 -{descent}",
        "STARTPROPERTIES 2",
        f"FONT_ASCENT {ascent}",
        f"FONT_DESCENT {descent}",
        "ENDPROPERTIES",
        f"CHARS {last - first + 1}",
    ]
    for codepoint in range(first, last + 1):
        lines += glyph_block(font, codepoint)
    lines.append("ENDFONT")

    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print(
        f"{args.output}: {last - first + 1} glyphs at {args.size} px, "
        f"ascent {ascent}, descent {descent}, line height {line_height}"
    )


if __name__ == "__main__":
    main()
