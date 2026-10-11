#!/usr/bin/env python3
"""abprep.py -- the Tier A table: every image, how stock MAME loads it, and on
which console.

    abprep.py ROMDIR OUTDIR

Each image's A side is an .a78 file whose header names the board our mapper
database gives it, so stock MAME runs it on its own cart device: the MAME
layout for No-Intro's Activision dumps (8K halves swapped back), the image
as it is otherwise. The High Score Cart has no header type and loads from
MAME's software list ("hiscore") instead. Writes OUTDIR/table.tsv (id, file,
A-side path or list name, system, note) and OUTDIR/a/<id>.a78.
"""

import os
import re
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYPE = {"A78MAP_ROM": 0x0000, "A78MAP_POKEY": 0x0001, "A78MAP_SG": 0x0002,
        "A78MAP_SG_POKEY": 0x0003, "A78MAP_SG_RAM": 0x0006, "A78MAP_SG9": 0x000A,
        "A78MAP_MRAM": 0x0080, "A78MAP_ABS": 0x0200, "A78MAP_ACT": 0x0100}


def db():
    rows = {}
    for l in open(os.path.join(HERE, "firmware", "src", "a78map_db.c")):
        m = re.match(r"\s*\{ 0x([0-9A-F]+)u, (\w+), (\w+), (\d+), (\d+) \},\s*/\* (.*) \*/", l)
        if m:
            rows[int(m.group(1), 16)] = (m.group(2), m.group(3) != "0", m.group(6))
    return rows


def system_for(name):
    tags = " ".join(re.findall(r"\(([^)]*)\)", name))
    return "a7800p" if "Europe" in tags and "USA" not in tags else "a7800"


def swap8k(data):
    out = bytearray(len(data))
    for off in range(0, len(data), 0x2000):
        out[off ^ 0x2000:(off ^ 0x2000) + 0x2000] = data[off:off + 0x2000]
    return bytes(out)


def header(data, mapper, title):
    h = bytearray(128)
    h[0] = 1
    h[1:10] = b"ATARI7800"
    t = title.encode("ascii", "replace")[:32]
    h[17:17 + len(t)] = t
    h[49:53] = len(data).to_bytes(4, "big")
    h[53:55] = mapper.to_bytes(2, "big")
    h[55] = h[56] = 1
    h[100:128] = b"ACTUAL CART DATA STARTS HERE"
    return bytes(h) + data


def main():
    romdir, out = sys.argv[1], os.path.abspath(sys.argv[2])
    rows_db = db()
    os.makedirs(os.path.join(out, "a"), exist_ok=True)
    rows = []
    for f in sorted(os.listdir(romdir)):
        data = open(os.path.join(romdir, f), "rb").read()
        kind, swap, name = rows_db.get(zlib.crc32(data), ("A78MAP_ROM", False, "?"))
        if kind not in TYPE and kind != "A78MAP_HSC":
            # a board the cart refuses: nothing to compare
            print("abprep: %s: %s, skipped" % (f, kind), file=sys.stderr)
            continue
        rid = len(rows)
        note = "bios" if f.startswith("[BIOS]") else "-"
        if kind == "A78MAP_HSC":
            # the list's "hiscore" entry, from a rompath of its own
            d = os.path.join(out, "roms", "a7800", "hiscore")
            os.makedirs(d, exist_ok=True)
            open(os.path.join(d, "highscre.bin"), "wb").write(data)
            a = "hiscore"
            note = "hsc"
        else:
            if swap:
                data = swap8k(data)
            a = os.path.join(out, "a", "%d.a78" % rid)
            open(a, "wb").write(header(data, TYPE[kind], os.path.splitext(f)[0]))
        rows.append((rid, f, a, system_for(f), note))
    with open(os.path.join(out, "table.tsv"), "w") as t:
        for r in rows:
            t.write("%d\t%s\t%s\t%s\t%s\n" % r)
    print("abprep: %d images" % len(rows))


if __name__ == "__main__":
    main()
