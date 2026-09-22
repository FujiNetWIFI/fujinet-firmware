#!/usr/bin/env python3
"""Render a binary ROM image as a C array header -- `xxd -i -n <symbol>` without xxd.

Usage: rom2h.py <symbol> <rom file> <output header>

The fujiversal pico firmware includes its cartridge ROM as build/<BOARD>/rom.h (see that
tree's board_defs.h). Upstream's Makefile generates it with xxd, which is not installed
everywhere -- upstream hit this too (commit 929e2f8) and worked around it by running xxd
inside their Docker image. The ESP32 build has neither, so it calls this instead, via the
pico_prebuild key in the fujiversal board inis.

Output matches `xxd -i` closely enough for board_defs.h: an unsigned char array plus an
unsigned int length, both named after <symbol>. Writes only when the content changes, so an
unchanged ROM does not force the pico firmware to relink on every build.
"""

import os
import sys

BYTES_PER_LINE = 12


def render(symbol, data):
    lines = [f"unsigned char {symbol}[] = {{"]
    for start in range(0, len(data), BYTES_PER_LINE):
        chunk = data[start:start + BYTES_PER_LINE]
        row = ", ".join(f"0x{b:02x}" for b in chunk)
        trailing = "," if start + BYTES_PER_LINE < len(data) else ""
        lines.append(f"  {row}{trailing}")
    lines.append("};")
    lines.append(f"unsigned int {symbol}_len = {len(data)};")
    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) != 4:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    symbol, rom_path, out_path = argv[1], argv[2], argv[3]

    if not symbol.isidentifier():
        print(f"rom2h.py: {symbol!r} is not a valid C identifier", file=sys.stderr)
        return 1

    try:
        with open(rom_path, "rb") as fh:
            data = fh.read()
    except OSError as err:
        print(f"rom2h.py: cannot read ROM image {rom_path}: {err}", file=sys.stderr)
        return 1

    if not data:
        print(f"rom2h.py: ROM image {rom_path} is empty", file=sys.stderr)
        return 1

    rendered = render(symbol, data)

    # Don't rewrite an identical header: the pico firmware's build depends on this file, and
    # a fresh mtime alone would rebuild (and relink) it on every ESP32 build.
    try:
        with open(out_path, "r") as fh:
            if fh.read() == rendered:
                print(f"rom2h.py: {out_path} already up to date ({len(data)} bytes)")
                return 0
    except OSError:
        pass

    parent = os.path.dirname(os.path.abspath(out_path))
    if parent:
        os.makedirs(parent, exist_ok=True)

    try:
        with open(out_path, "w") as fh:
            fh.write(rendered)
    except OSError as err:
        print(f"rom2h.py: cannot write {out_path}: {err}", file=sys.stderr)
        return 1

    print(f"rom2h.py: wrote {out_path} ({len(data)} bytes as {symbol}[])")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
