#!/usr/bin/env python3
"""dispcheck.py -- decode a MAME snapshot back into the six text planes and
byte-compare them with tools/vcsfont.py's render of the same text, so a glyph
drawn in the wrong column group fails rather than looking right.

Usage: dispcheck.py <snapshot.png> <screen.txt>
"""

import sys

sys.path.insert(0, __file__.rsplit('/', 2)[0] + '/tools')
from vcsfont import pack_screen, PLANES, PLANE_LEN, CELL_H, ROWS   # noqa: E402

GROUPS = 6
BLOCK_W = GROUPS * 8          # 48 pixels
LINES = ROWS * CELL_H         # 126


def load_ink(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    # Background is the darkest color present; ink is anything brighter.
    lum = [[sum(px[x, y]) for x in range(w)] for y in range(h)]
    flat = sorted(v for row in lum for v in row)
    bg = flat[len(flat) // 2]          # the median pixel is background
    return [[lum[y][x] > bg + 60 for x in range(w)] for y in range(h)], w, h


def main():
    snap, screen = sys.argv[1], sys.argv[2]
    ink, w, h = load_ink(snap)

    cols = [x for x in range(w) if any(ink[y][x] for y in range(h))]
    rows = [y for y in range(h) if any(ink[y][x] for x in range(w))]
    if not cols or not rows:
        print("FAIL: nothing drawn at all")
        return 1

    lines = open(screen).read().split("\n")
    want = pack_screen(lines)

    def decode(x0, y0):
        got = bytearray(PLANES * PLANE_LEN)
        for i in range(LINES):
            y = y0 + i
            if not (0 <= y < h):
                return None
            for g in range(GROUPS):
                b = 0
                for bit in range(8):
                    x = x0 + g * 8 + bit
                    if 0 <= x < w and ink[y][x]:
                        b |= 1 << (7 - bit)
                got[g * PLANE_LEN + i] = b
        return got

    # Search for the block origin: the leftmost ink is column 0 only once the
    # display is already right.
    best, best_bad, got = None, None, None
    for x0 in range(min(cols) - 8, min(cols) + 9):
        for y0 in range(min(rows) - 2, min(rows) + 3):
            g = decode(x0, y0)
            if g is None:
                continue
            n = sum(1 for i in range(len(want))
                    if (i % PLANE_LEN) < LINES and want[i] != g[i])
            if best_bad is None or n < best_bad:
                best, best_bad, got = (x0, y0), n, g
    x0, y0 = best
    print("block origin x=%d y=%d (searched)" % (x0, y0))

    bad = [i for i in range(len(want))
           if (i % PLANE_LEN) < LINES and want[i] != got[i]]
    if bad:
        i = bad[0]
        plane, idx = i // PLANE_LEN, i % PLANE_LEN
        row, scan = idx // CELL_H, idx % CELL_H
        print("FAIL: %d of %d bytes differ. First at plane %d (text columns "
              "%d-%d), row %d, scanline %d: want $%02X got $%02X"
              % (len(bad), LINES * GROUPS, plane, plane * 2, plane * 2 + 1,
                 row, scan, want[i], got[i]))
        print("      row %d text is %r" % (row, lines[row] if row < len(lines) else ""))
        return 1

    print("PASS: %d scanlines x %d column groups recovered from the raster, "
          "all byte-identical to the renderer (%d text rows x %d columns)"
          % (LINES, GROUPS, ROWS, 12))
    return 0


if __name__ == "__main__":
    sys.exit(main())
