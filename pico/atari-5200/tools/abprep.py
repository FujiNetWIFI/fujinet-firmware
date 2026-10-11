#!/usr/bin/env python3
"""abprep.py -- Tier A's table: every image in the corpus, the A side MAME
loads for it, and the console.

    abprep.py [corpus-dir]        (default $A52_ROMS or /tmp/a5200)

Stock MAME picks a 5200 mapper by path only from a .car header or a .hsi
A13MIRRORING line, and five 2-chip images have no such line, so the A side
is always told: each image is wrapped as a .car of the type the cart's own
plan chose (type 7 for Bounty Bob, which MAME reads in its own file order --
the order the corpus is in). A 2K image has no .car type and goes in raw,
as a5200_rom. Writes build/ab/table.tsv and build/ab/a/.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AB = os.path.join(HERE, "build", "ab")
A52PLAN = os.path.join(HERE, "build", "tools", "a52plan")
ROM_TYPES = {0x8000: 4, 0x4000: 16, 0x2000: 19, 0x1000: 20}


def plan(path):
    out = subprocess.run([A52PLAN, path], capture_output=True, text=True,
                         env=dict(os.environ, FUJINET_MAPPER="")).stdout
    return dict(kv.split("=", 1) for kv in out.split())


def car(data, ctype):
    head = b"CART" + ctype.to_bytes(4, "big") + (sum(data) & 0xFFFFFFFF).to_bytes(4, "big") + bytes(4)
    return head + data


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return s[:40]


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("A52_ROMS", "/tmp/a5200")
    os.makedirs(os.path.join(AB, "a"), exist_ok=True)
    rows = []
    seen = set()
    for f in sorted(os.listdir(src)):
        if not f.lower().endswith((".a52", ".bin")):
            continue
        path = os.path.join(src, f)
        data = open(path, "rb").read()
        p = plan(path)
        rid = slug(os.path.splitext(f)[0])
        while rid in seen:
            rid += "x"
        seen.add(rid)
        kind = p.get("kind", "error")
        if kind == "a5200_2chips":
            a, note = car(data, 6), "car6"
        elif kind == "a5200_bbsb":
            a, note = car(data, 7), "car7"
        elif kind == "a5200_rom" and len(data) in ROM_TYPES:
            a, note = car(data, ROM_TYPES[len(data)]), "car%d" % ROM_TYPES[len(data)]
        else:
            a, note = data, "raw"
        ext = ".a52" if note == "raw" else ".car"
        apath = os.path.join(AB, "a", rid + ext)
        open(apath, "wb").write(a)
        rows.append((rid, f, apath, "a5200", "%s %s" % (kind, note)))
    with open(os.path.join(AB, "table.tsv"), "w") as out:
        for r in rows:
            out.write("\t".join(r) + "\n")
    print("abprep.py: %d images -> %s" % (len(rows), os.path.join(AB, "table.tsv")))


if __name__ == "__main__":
    main()
