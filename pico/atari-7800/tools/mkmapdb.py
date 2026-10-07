#!/usr/bin/env python3
"""mkmapdb.py -- the cart's mapper database, from MAME's 7800 software list.

Every a7800.xml entry becomes {CRC-32 of its whole image, mapper}. Multi-chip
entries are keyed by the CRC of their chips concatenated, which is what a
single-file dump of the same cart hashes to.

Cart RAM sizes come along: the 8K SuperGame boards mirror theirs.

With --corpus, each file there that differs from MAME's dump of the same cart
gets a row of its own: No-Intro's Activision images (each 16K bank's 8K
halves swapped, flagged so the slots undo it) and the 48K images MAME stores
as 64K with 16K of $FF in front. --biosok adds the per-console BIOS verdicts
tools/biosok.py measured (CRC, NTSC 0/1, PAL 0/1, per line).

Usage: mkmapdb.py ~/Workspace/mame/hash/a7800.xml [--corpus DIR]
                  [--biosok FILE] > firmware/src/a78map_db.c
"""

import argparse
import os
import sys
import xml.etree.ElementTree as ET
import zlib

KINDS = {"a78_rom": "A78MAP_ROM", "a78_pokey": "A78MAP_POKEY",
         "a78_sg": "A78MAP_SG", "a78_sg_pokey": "A78MAP_SG_POKEY",
         "a78_sg_ram": "A78MAP_SG_RAM", "a78_sg9": "A78MAP_SG9",
         "a78_mram": "A78MAP_MRAM", "a78_abs": "A78MAP_ABS",
         "a78_act": "A78MAP_ACT", "a78_hsc": "A78MAP_HSC"}


def gf2_times(mat, vec):
    s = 0
    i = 0
    while vec:
        if vec & 1:
            s ^= mat[i]
        vec >>= 1
        i += 1
    return s


def gf2_square(mat):
    return [gf2_times(mat, mat[n]) for n in range(32)]


def crc32_combine(crc1, crc2, len2):
    """zlib's crc32_combine: the CRC of A+B from crc(A), crc(B), len(B)."""
    if len2 == 0:
        return crc1
    odd = [0xEDB88320] + [1 << n for n in range(31)]
    even = gf2_square(odd)
    odd = gf2_square(even)
    while True:
        even = gf2_square(odd)
        if len2 & 1:
            crc1 = gf2_times(even, crc1)
        len2 >>= 1
        if not len2:
            break
        odd = gf2_square(even)
        if len2 & 1:
            crc1 = gf2_times(odd, crc1)
        len2 >>= 1
        if not len2:
            break
    return crc1 ^ crc2


def num(s):
    return int(s, 0)


def softlist(path):
    """{crc: (slot, name, ram KiB)} for every a7800 cart entry."""
    rows = {}
    for sw in ET.parse(path).getroot().iter("software"):
        for part in sw.iter("part"):
            if part.get("interface") != "a7800_cart":
                continue
            slot = "a78_rom"
            for f in part.iter("feature"):
                if f.get("name") == "slot":
                    slot = f.get("value")
            roms, ram = [], 0
            for da in part.iter("dataarea"):
                if da.get("name") == "ram":
                    ram = num(da.get("size")) // 1024
                if da.get("name") == "rom":
                    for r in da.iter("rom"):
                        if r.get("crc") and r.get("size"):
                            roms.append((num(r.get("offset", "0")), num(r.get("size")),
                                         int(r.get("crc"), 16)))
            if not roms:
                continue
            roms.sort()
            crc = roms[0][2]
            for _, size, c in roms[1:]:
                crc = crc32_combine(crc, c, size)
            rows.setdefault(crc, (slot, sw.get("name"), ram))
    return rows


def swap8k(data):
    out = bytearray(len(data))
    for off in range(0, len(data), 0x2000):
        out[off ^ 0x2000:(off ^ 0x2000) + 0x2000] = data[off:off + 0x2000]
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hash")
    ap.add_argument("--corpus")
    ap.add_argument("--biosok")
    args = ap.parse_args()

    soft = softlist(args.hash)
    rows = {crc: (KINDS.get(slot, "A78MAP_UNSUPPORTED"), 0, name, ram)
            for crc, (slot, name, ram) in soft.items()}

    if args.corpus:
        for fn in sorted(os.listdir(args.corpus)):
            data = open(os.path.join(args.corpus, fn), "rb").read()
            crc = zlib.crc32(data)
            if crc in rows:
                continue
            if len(data) % 0x4000 == 0 and zlib.crc32(swap8k(data)) in soft:
                slot, name, ram = soft[zlib.crc32(swap8k(data))]
                rows[crc] = (KINDS.get(slot, "A78MAP_UNSUPPORTED"), 1, name + " (8K halves swapped)", ram)
            elif zlib.crc32(b"\xff" * 0x4000 + data) in soft:
                slot, name, ram = soft[zlib.crc32(b"\xff" * 0x4000 + data)]
                rows[crc] = (KINDS.get(slot, "A78MAP_UNSUPPORTED"), 0, name + " (top 48K)", ram)
            else:
                # not MAME's dump: a78map's size heuristic, so the row can
                # carry the BIOS verdict
                kind = ("A78MAP_ROM" if len(data) <= 0xC000 else
                        "A78MAP_SG9" if len(data) == 0x24000 else "A78MAP_SG")
                rows[crc] = (kind, 0, os.path.splitext(fn)[0].replace("*/", ""), 0)

    biosok = {}
    if args.biosok:
        for line in open(args.biosok):
            f = line.split()
            if len(f) >= 3 and not f[0].startswith("#"):
                biosok[int(f[0], 16)] = (int(f[1]) and 1) | (int(f[2]) and 2)

    out = sys.stdout
    out.write("/* GENERATED by pico/atari-7800/tools/mkmapdb.py from MAME hash/a7800.xml"
              " (CC0-1.0) -- do not edit. */\n\n")
    out.write('#include "a78map_db.h"\n\n')
    out.write("const a78map_db_t a78map_db[] = {\n")
    for crc in sorted(set(rows) | set(biosok)):
        kind, flags, name, ram = rows.get(crc, ("A78MAP_UNSUPPORTED", 0, "?", 0))
        ok = biosok.get(crc, 0)
        if kind == "A78MAP_UNSUPPORTED" and crc not in rows:
            continue
        out.write("    { 0x%08Xu, %s, %s, %u, %u },  /* %s */\n"
                  % (crc, kind, "A78DB_SWAP8K" if flags else "0", ok, ram, name))
    out.write("};\n\n")
    out.write("const unsigned a78map_db_count = sizeof a78map_db / sizeof a78map_db[0];\n")


if __name__ == "__main__":
    main()
