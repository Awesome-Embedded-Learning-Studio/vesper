#!/usr/bin/env python3
"""shot_gate.py -- the VM lane's pixel judgment.

Reads the QEMU screendump (a binary P6 PPM) and counts the two inks the
mono desk paints: lit pixels (the white panel) and ink pixels (the black
marks). The desk is 640x480 of mostly-white with icons, a panel strip and
captions on it, so a painted frame carries both in quantity; a boot log or
a dead black frame fails one of the two. Thresholds sit far below the
expected counts so a differently-sized guest framebuffer still judges the
same paint.
"""

import sys

LIT_MIN = 200   # a channel at or above this reads as lit panel
INK_MAX = 64    # a channel at or below this reads as ink
LIT_GATE = 80000
INK_GATE = 1000


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise SystemExit(f"shot_gate: {path} is not a binary PPM (P6)")
    fields = []
    pos = 2
    while len(fields) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while pos < len(data) and data[pos : pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # exactly one whitespace after maxval
    width, height, _maxval = fields
    return width, height, data[pos : pos + width * height * 3]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} shot.ppm")
    width, height, pixels = read_ppm(sys.argv[1])
    if len(pixels) != width * height * 3:
        raise SystemExit(
            f"shot_gate: truncated PPM body ({len(pixels)} bytes for {width}x{height})"
        )
    lit = ink = 0
    for r, g, b in zip(pixels[0::3], pixels[1::3], pixels[2::3]):
        if r >= LIT_MIN and g >= LIT_MIN and b >= LIT_MIN:
            lit += 1
        elif r <= INK_MAX and g <= INK_MAX and b <= INK_MAX:
            ink += 1
    print(f"shot_gate: {width}x{height} lit={lit} ink={ink}")
    ok = True
    if lit < LIT_GATE:
        print(f"shot_gate: FAIL lit {lit} < {LIT_GATE} (no painted panel?)")
        ok = False
    if ink < INK_GATE:
        print(f"shot_gate: FAIL ink {ink} < {INK_GATE} (no marks on the panel?)")
        ok = False
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
