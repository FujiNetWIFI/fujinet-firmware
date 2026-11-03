#!/usr/bin/env python3
"""vcsfont.py -- the 3x5 font and the row renderer for the Atari 2600 port.

This is the REFERENCE implementation of what the cartridge does in C
(firmware/src/vcs_render.c). Written first, and independently, so the host
test can byte-compare the two: the ColecoVision port's lesson is that an
expectation which calls the code under test proves nothing.

Geometry, from firmware/include/fuji_mailbox.h:
  - 12 text columns across the 48 pixels two players can reach;
  - a 3x5 glyph in a 4x6 cell -- 3 pixels of ink, 1 of gap, 5 rows of ink,
    1 blank row of leading;
  - six bytes per scanline, six scanlines per row = 36 bytes per row;
  - within a row the layout is COLUMN-GROUP MAJOR: group g (two text columns,
    eight pixels, one player write) owns bytes [g*6 .. g*6+5], one per
    scanline. That is what lets the kernel hold six zero-page pointers at
    row_base + 0,6,12,18,24,30 and index them all with Y = scanline.

GRP0/GRP1 put bit 7 leftmost, so text column 2g lands in bits 7-5 with bit 4
as its gap, and column 2g+1 in bits 3-1 with bit 0 as its gap.
"""

COLS = 12
CELL_H = 6
INK_H = 5
GROUPS = 6
STRIDE = GROUPS * CELL_H          # 36

# 3x5, row-major, '#' = ink. Lowercase is mapped to uppercase for now; the
# Channel F port derives real lowercase by squashing to 4 rows, and that is
# the follow-up once a password field needs it.
_F = {
    ' ': ("...", "...", "...", "...", "..."),
    '!': (".#.", ".#.", ".#.", "...", ".#."),
    '"': ("#.#", "#.#", "...", "...", "..."),
    '#': ("#.#", "###", "#.#", "###", "#.#"),
    '$': (".##", "##.", ".##", "##.", ".#."),
    '%': ("#.#", "..#", ".#.", "#..", "#.#"),
    '&': ("##.", "##.", "###", "#.#", "###"),
    "'": (".#.", ".#.", "...", "...", "..."),
    '(': ("..#", ".#.", ".#.", ".#.", "..#"),
    ')': ("#..", ".#.", ".#.", ".#.", "#.."),
    '*': ("#.#", ".#.", "###", ".#.", "#.#"),
    '+': ("...", ".#.", "###", ".#.", "..."),
    ',': ("...", "...", "...", ".#.", "#.."),
    '-': ("...", "...", "###", "...", "..."),
    '.': ("...", "...", "...", "...", ".#."),
    '/': ("..#", "..#", ".#.", "#..", "#.."),
    '0': (".#.", "#.#", "#.#", "#.#", ".#."),   # rounded, vs square 'O'
    '1': (".#.", "##.", ".#.", ".#.", "###"),
    '2': ("###", "..#", "###", "#..", "###"),
    '3': ("###", "..#", "###", "..#", "###"),
    '4': ("#.#", "#.#", "###", "..#", "..#"),
    '5': ("###", "#..", "###", "..#", "###"),   # flat top, vs curved 'S'
    '6': ("###", "#..", "###", "#.#", "###"),
    '7': ("###", "..#", "..#", "..#", "..#"),
    '8': ("###", "#.#", "###", "#.#", "###"),
    '9': ("###", "#.#", "###", "..#", "###"),
    ':': ("...", ".#.", "...", ".#.", "..."),
    ';': ("...", ".#.", "...", ".#.", "#.."),
    '<': ("..#", ".#.", "#..", ".#.", "..#"),
    '=': ("...", "###", "...", "###", "..."),
    '>': ("#..", ".#.", "..#", ".#.", "#.."),
    '?': ("###", "..#", ".##", "...", ".#."),
    '@': ("###", "#.#", "###", "#..", "###"),
    'A': ("###", "#.#", "###", "#.#", "#.#"),
    'B': ("##.", "#.#", "##.", "#.#", "##."),
    'C': ("###", "#..", "#..", "#..", "###"),
    'D': ("##.", "#.#", "#.#", "#.#", "##."),
    'E': ("###", "#..", "##.", "#..", "###"),
    'F': ("###", "#..", "##.", "#..", "#.."),
    'G': ("###", "#..", "#.#", "#.#", "###"),
    'H': ("#.#", "#.#", "###", "#.#", "#.#"),
    'I': ("###", ".#.", ".#.", ".#.", "###"),
    'J': ("..#", "..#", "..#", "#.#", "###"),
    'K': ("#.#", "#.#", "##.", "#.#", "#.#"),
    'L': ("#..", "#..", "#..", "#..", "###"),
    'M': ("#.#", "###", "###", "#.#", "#.#"),
    'N': ("##.", "#.#", "#.#", "#.#", "#.#"),
    'O': ("###", "#.#", "#.#", "#.#", "###"),
    'P': ("###", "#.#", "###", "#..", "#.."),
    'Q': ("###", "#.#", "#.#", "###", "..#"),
    'R': ("###", "#.#", "##.", "#.#", "#.#"),
    'S': (".##", "#..", ".#.", "..#", "##."),   # curved, vs flat-top '5'
    'T': ("###", ".#.", ".#.", ".#.", ".#."),
    'U': ("#.#", "#.#", "#.#", "#.#", "###"),
    'V': ("#.#", "#.#", "#.#", "#.#", ".#."),
    'W': ("#.#", "#.#", "###", "###", "#.#"),
    'X': ("#.#", "#.#", ".#.", "#.#", "#.#"),
    'Y': ("#.#", "#.#", ".#.", ".#.", ".#."),
    'Z': ("###", "..#", ".#.", "#..", "###"),
    '[': ("##.", "#..", "#..", "#..", "##."),   # half-width, vs 'C'
    '\\': ("#..", "#..", ".#.", "..#", "..#"),
    ']': ("###", "..#", "..#", "..#", "###"),
    '^': (".#.", "#.#", "...", "...", "..."),
    '_': ("...", "...", "...", "...", "###"),
    '`': ("#..", ".#.", "...", "...", "..."),
    '{': ("..#", ".#.", "##.", ".#.", "..#"),
    '|': (".#.", ".#.", ".#.", ".#.", ".#."),
    '}': ("#..", ".#.", ".##", ".#.", "#.."),
    '~': ("...", "..#", "###", "#..", "..."),
}


def glyph(ch):
    """The five 3-bit ink rows of `ch`, as ints 0-7, MSB leftmost."""
    if ch.islower():
        ch = ch.upper()
    rows = _F.get(ch, _F['?'])
    out = []
    for r in rows:
        v = 0
        if r[0] == '#':
            v |= 4
        if r[1] == '#':
            v |= 2
        if r[2] == '#':
            v |= 1
        out.append(v)
    return tuple(out)


def render_row(text):
    """Render `text` into one 36-byte render-window row.

    Truncated, not wrapped, at COLS -- a client that streams a long filename
    gets it cut off rather than corrupting the next row.
    """
    chars = (list(text[:COLS]) + [' '] * COLS)[:COLS]
    out = bytearray(STRIDE)
    for g in range(GROUPS):
        left = glyph(chars[2 * g])
        right = glyph(chars[2 * g + 1])
        for s in range(CELL_H):
            if s < INK_H:
                # column 2g   -> bits 7-5, bit 4 is its gap
                # column 2g+1 -> bits 3-1, bit 0 is its gap
                out[g * CELL_H + s] = (left[s] << 5) | (right[s] << 1)
            else:
                out[g * CELL_H + s] = 0          # the blank leading row
    return bytes(out)


PLANES = 6
PLANE_LEN = 128                   # FN_T_PLANE_LEN -- the alignment matters
ROWS = 21                         # FN_T_ROWS: 21 * 6 = 126 <= 128


def pack_screen(lines):
    """Pack up to ROWS lines of text into the 768-byte text-plane block.

    Output is the six planes back to back, exactly as they sit at
    $1800-$1AFF. Plane p, offset Y = row * CELL_H + scanline, holds column
    group p of that row -- which is what lets the kernel index all six with
    one Y that simply counts 0..125 down the screen, with no per-row setup
    and no zero-page pointers.
    """
    out = bytearray(PLANES * PLANE_LEN)
    for r in range(ROWS):
        text = lines[r] if r < len(lines) else ""
        row = render_row(text)
        for g in range(GROUPS):
            for s in range(CELL_H):
                out[g * PLANE_LEN + r * CELL_H + s] = row[g * CELL_H + s]
    return bytes(out)


def emit_inc(lines, label="TPLANE"):
    """Emit the packed screen as an AS include, one plane per ORG'd block."""
    blob = pack_screen(lines)
    outl = []
    for p in range(PLANES):
        chunk = blob[p * PLANE_LEN:(p + 1) * PLANE_LEN]
        outl.append("%s%d:" % (label, p))
        for i in range(0, PLANE_LEN, 16):
            outl.append("\tDB\t" + ",".join("$%02X" % b for b in chunk[i:i + 16]))
    return "\n".join(outl)


if __name__ == "__main__":
    import sys
    if sys.argv[1:2] == ["--screen-file"]:
        # One source of truth for the screen: emu/dispcheck.py renders the
        # same file to build its expectation, so the baked image and the test
        # can never drift apart.
        print("; generated by tools/vcsfont.py -- do not edit")
        print(emit_inc(open(sys.argv[2]).read().split("\n")))
    elif sys.argv[1:2] == ["--screen"]:
        print("; generated by tools/vcsfont.py -- do not edit")
        print(emit_inc(sys.argv[2:]))
    else:
        for line in sys.argv[1:]:
            print('; "%s"' % line)
            print("\tDB\t" + ",".join("$%02X" % b for b in render_row(line)))
