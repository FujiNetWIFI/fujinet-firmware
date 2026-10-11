#!/usr/bin/env python3
"""abprep.py -- the Tier A table: every image, how stock MAME loads it, and on
which console.

    abprep.py ROMDIR MAME_HASH_XML OUTDIR

Writes OUTDIR/table.tsv (id, file, softlist name or "-", system, note, the
controllers the list plugs in or "-") and a
software-list rompath under OUTDIR/roms/sms/<name>/, so the A side loads each
known image by its list name and gets MAME's own board and RAM for it. Images
the list does not know are loaded by path, which is what MAME does with them
anyway. The Korean multicarts are out of scope and skipped.
"""

import hashlib
import os
import re
import sys
import xml.etree.ElementTree as ET

EXCLUDE = re.compile(r"Hap|in 1|Zemina Best|Zemina Rompack|Super Game|Super Multi Game|"
                     r"Chongjiphap|Mo-eumjip")


def system_for(name):
    tags = " ".join(re.findall(r"\(([^)]*)\)", name))
    ntsc = re.search(r"USA|World|Japan|Korea|Taiwan|Brazil", tags)
    pal = re.search(r"Europe|Australia", tags)
    return "smspal" if pal and not ntsc else "sms"


def main():
    romdir, xmlpath, out = sys.argv[1:4]
    soft = []
    ctrls = {}
    for sw in ET.parse(xmlpath).getroot().iter("software"):
        c = {f.get("name")[:5]: f.get("value") for f in sw.findall("sharedfeat")
             if f.get("name") in ("ctrl1_default", "ctrl2_default")}
        if c:
            ctrls[sw.get("name")] = ",".join("%s=%s" % kv for kv in sorted(c.items()))
        for part in sw.iter("part"):
            if part.get("interface") != "sms_cart":
                continue
            roms = []
            for da in part.iter("dataarea"):
                if da.get("name") == "rom":
                    for r in da.iter("rom"):
                        if r.get("sha1"):
                            roms.append((int(r.get("offset", "0"), 0), int(r.get("size"), 0),
                                         r.get("name"), r.get("sha1")))
            if roms:
                soft.append((sw.get("name"), sorted(roms)))

    by_sha = {}
    for name, roms in soft:
        if len(roms) == 1:
            by_sha[roms[0][3]] = (name, roms)

    os.makedirs(os.path.join(out, "roms", "sms"), exist_ok=True)
    rows = []
    for n, f in enumerate(sorted(os.listdir(romdir))):
        if not f.endswith(".sms") or EXCLUDE.search(f):
            continue
        data = open(os.path.join(romdir, f), "rb").read()
        sha = hashlib.sha1(data).hexdigest()
        hit = by_sha.get(sha)
        if not hit:
            for name, roms in soft:          # multi-chip entries: slice the file
                if len(roms) > 1 and all(
                        hashlib.sha1(data[o:o + s]).hexdigest() == h for o, s, _, h in roms):
                    hit = (name, roms)
                    break
        note = "-"
        if f.startswith("[BIOS]"):
            note = "bios8k" if len(data) == 0x2000 else "bios"
        if hit:
            name, roms = hit
            d = os.path.join(out, "roms", "sms", name)
            os.makedirs(d, exist_ok=True)
            for o, s, rname, _ in roms:
                p = os.path.join(d, rname)
                if not os.path.exists(p):
                    open(p, "wb").write(data[o:o + s])
        else:
            name = "-"
        rows.append((len(rows), f, name, system_for(f), note, ctrls.get(name, "-")))

    with open(os.path.join(out, "table.tsv"), "w") as t:
        for r in rows:
            t.write("%d\t%s\t%s\t%s\t%s\t%s\n" % r)
    known = sum(1 for r in rows if r[2] != "-")
    print("abprep: %d images, %d in the software list, %d by path" % (len(rows), known, len(rows) - known))


if __name__ == "__main__":
    main()
