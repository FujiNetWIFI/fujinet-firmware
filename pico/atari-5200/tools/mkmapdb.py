#!/usr/bin/env python3
"""mkmapdb.py -- generate firmware/src/a52map_db.c, the cart's CRC database.

Sources, in priority order:
  MAME hash/a5200.xml   the software list's `slot` feature (CC0-1.0)
  MAME hash/a5200.hsi   A13MIRRORING / TYPE16 / BOUNTYBOB hints (CC0-1.0)
  tools/corpus.tsv      No-Intro images neither file lists, decided offline

Multi-chip software list entries (one ROM per socket) are matched against a
corpus directory, when given, so the whole-image CRC the cart sees is known.

  mkmapdb.py --mame ~/Workspace/mame [--corpus /tmp/a5200] -o firmware/src/a52map_db.c
  mkmapdb.py --mame ... --corpus /tmp/a5200 --classify tools/corpus.tsv
      (re)write corpus.tsv for the corpus images the two MAME files miss,
      using a52plan's 16K tracer; rows already decided by MAME are kept
"""

import argparse
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
import zlib

KINDS = {
    "a5200_rom": "A52MAP_ROM",
    "a5200_2chips": "A52MAP_2CHIPS",
    "a5200_bbsb": "A52MAP_BBSB",
    "a5200_supercart": "A52MAP_SUPERCART",
}
HSI = {"A13MIRRORING": "a5200_2chips", "TYPE16": "a5200_rom", "BOUNTYBOB": "a5200_bbsb"}

HERE = os.path.dirname(os.path.abspath(__file__))


def num(s):
    return int(s, 0)


def softlist(path):
    """[(name, slot, area_size, [(crc, size, offset)])]"""
    out = []
    for sw in ET.parse(path).getroot().iter("software"):
        part = sw.find("part")
        slot = None
        for f in part.iter("feature"):
            if f.get("name") == "slot":
                slot = f.get("value")
        area = part.find("dataarea")
        roms = []
        for r in area.iter("rom"):
            if r.get("crc"):
                roms.append((int(r.get("crc"), 16), num(r.get("size")),
                             num(r.get("offset", "0"))))
        out.append((sw.get("name"), slot or "a5200_rom", num(area.get("size")), roms))
    return out


def hsi(path):
    out = {}
    for h in ET.parse(path).getroot().iter("hash"):
        e = h.find("extrainfo")
        if e is not None and e.text in HSI:
            out[int(h.get("crc32"), 16)] = (HSI[e.text], h.get("name"))
    return out


def corpus_files(d):
    if not d:
        return []
    return sorted(os.path.join(d, f) for f in os.listdir(d)
                  if f.lower().endswith((".a52", ".bin", ".rom")))


def corpus_tsv(path):
    rows = {}
    if os.path.exists(path):
        for line in open(path):
            if line.startswith("#") or not line.strip():
                continue
            crc, slot, src, name = line.rstrip("\n").split("\t", 3)
            rows[int(crc, 16)] = (slot, src, name)
    return rows


def build(args):
    db = {}         # crc -> (slot, comment)
    multi = []
    for name, slot, size, roms in softlist(os.path.join(args.mame, "hash/a5200.xml")):
        if len(roms) == 1 and roms[0][1] == size:
            db[roms[0][0]] = (slot, name)
        else:
            multi.append((name, slot, size, roms))
    for path in corpus_files(args.corpus):
        data = open(path, "rb").read()
        for name, slot, size, roms in multi:
            if len(data) == size and all(
                    zlib.crc32(data[o:o + n]) == c for c, n, o in roms):
                db[zlib.crc32(data)] = (slot, name + " (parts joined)")
    for crc, (slot, name) in hsi(os.path.join(args.mame, "hash/a5200.hsi")).items():
        db.setdefault(crc, (slot, name + " (.hsi)"))
    for crc, (slot, src, name) in corpus_tsv(args.tsv).items():
        db.setdefault(crc, (slot, "%s (%s)" % (name, src)))
    return db


def classify(args, db):
    a52plan = os.path.join(HERE, "..", "build", "tools", "a52plan")
    if not os.path.exists(a52plan):
        sys.exit("mkmapdb.py: build %s first (make -C tools)" % a52plan)
    old = corpus_tsv(args.classify)
    rows = {}
    for path in corpus_files(args.corpus):
        data = open(path, "rb").read()
        crc = zlib.crc32(data)
        name = os.path.splitext(os.path.basename(path))[0]
        if crc in db:
            continue
        if crc in old and old[crc][1] == "mame":
            rows[crc] = old[crc]
            continue
        if len(data) == 0x4000:
            kind = subprocess.run([a52plan, "--guess16k", path], check=True,
                                  capture_output=True, text=True).stdout.split()[0]
            rows[crc] = (kind, "trace", name)
        elif len(data) <= 0x8000:
            rows[crc] = ("a5200_rom", "size", name)
        elif len(data) == 0xA000:
            rows[crc] = ("a5200_bbsb", "size", name)
    with open(args.classify, "w") as f:
        f.write("# crc\tslot\tsource\tname -- corpus images MAME's hash files miss.\n"
                "# source: size (unambiguous by size), trace (a52plan --guess16k),\n"
                "# mame (booted natively under both mappings; the one that ran).\n")
        for crc in sorted(rows):
            slot, src, name = rows[crc]
            f.write("%08x\t%s\t%s\t%s\n" % (crc, slot, src, name))
    print("mkmapdb.py: %d rows -> %s" % (len(rows), args.classify))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mame", required=True)
    ap.add_argument("--corpus")
    ap.add_argument("--tsv", default=os.path.join(HERE, "corpus.tsv"))
    ap.add_argument("--classify")
    ap.add_argument("-o", "--out")
    args = ap.parse_args()

    if args.classify:
        args.tsv = "/dev/null"
        classify(args, build(args))
        return
    db = build(args)
    lines = ["/* GENERATED by pico/atari-5200/tools/mkmapdb.py from MAME hash/a5200.xml"
             " and hash/a5200.hsi (CC0-1.0) and tools/corpus.tsv -- do not edit. */",
             "", '#include "a52map_db.h"', "", "const a52map_db_t a52map_db[] = {"]
    for crc in sorted(db):
        slot, name = db[crc]
        lines.append("    { 0x%08Xu, %s },  /* %s */" % (crc, KINDS[slot], name.replace("*/", "* /")))
    lines += ["};", "", "const unsigned a52map_db_count = sizeof a52map_db / sizeof a52map_db[0];", ""]
    text = "\n".join(lines)
    if args.out:
        open(args.out, "w").write(text)
        print("mkmapdb.py: %d entries -> %s" % (len(db), args.out))
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
