#!/usr/bin/env python3
"""classify16k.py -- ground truth for the 16K images tools/corpus.tsv decided
by tracing: boot each natively in stock MAME as a 2-chip board and as a
linear one, with abtest.lua's inputs, and see which one runs.

    classify16k.py [-j N] [--write]

A mapping "runs" when its frames are not blank and at least 8 of the 30 differ;
the wrong one typically shows the BIOS title and crashes, 1-4 distinct frames.
Where one runs and the other plainly does not, that is the answer; otherwise
the tracer's verdict stands and the row says so. --write updates corpus.tsv:
a decided row gets source "mame".
"""

import argparse
import concurrent.futures as cf
import hashlib
import os
import shutil
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, "tools"))
import abprep  # noqa: E402
import abrun   # noqa: E402

TSV = os.path.join(HERE, "tools", "corpus.tsv")
WORK = os.path.join(HERE, "build", "classify16k")
ROMDIR = os.environ.get("A52_ROMS", "/tmp/a5200")


def rows():
    out = []
    for line in open(TSV):
        if line.startswith("#") or not line.strip():
            continue
        crc, slot, src, name = line.rstrip("\n").split("\t", 3)
        out.append([crc, slot, src, name])
    return out


def find(name):
    for f in os.listdir(ROMDIR):
        if os.path.splitext(f)[0] == name:
            return f
    return None


def liveness(shots):
    if not shots:
        return 0, 0
    hashes = [hashlib.md5(open(p, "rb").read()).hexdigest() for p in shots]
    blank = sum(1 for p in shots if abrun.blank(p))
    return len(set(hashes)), blank


def trial(row):
    crc, slot, src, name = row
    f = find(name)
    data = open(os.path.join(ROMDIR, f), "rb").read()
    res = {}
    for kind, ctype in (("a5200_2chips", 6), ("a5200_rom", 16)):
        rid = "%s-%s" % (crc, ctype)
        path = os.path.join(WORK, rid + ".car")
        open(path, "wb").write(abprep.car(data, ctype))
        work, shots, rc, log = abrun.run("A", (rid, f, path, "a5200", ""))
        distinct, blank = liveness(shots)
        res[kind] = (distinct, blank, len(shots))
        shutil.rmtree(work, ignore_errors=True)
    runs = {k for k, (d, b, n) in res.items() if n and d >= 8 and b < n // 2}
    dead = {k for k, (d, b, n) in res.items() if not n or d <= 4}
    if len(runs) == 1 and len(dead) == 1:
        verdict = runs.pop()
        how = "mame"
    else:
        verdict = slot
        how = "trace (%s)" % ("both run" if runs else "neither runs")
    return row, verdict, how, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=12)
    ap.add_argument("--write", action="store_true")
    a = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    abrun.AB = WORK
    todo = [r for r in rows() if r[2] in ("trace", "mame")]
    decided = {}
    with cf.ThreadPoolExecutor(a.j) as ex:
        for row, verdict, how, res in ex.map(trial, todo):
            flag = "" if verdict == row[1] else "  <-- CHANGED"
            print("%s %-14s -> %-14s %-22s 2chips %s rom %s  %s%s" % (
                row[0], row[1], verdict, how, res["a5200_2chips"], res["a5200_rom"], row[3], flag))
            decided[row[0]] = (verdict, how)
    if a.write:
        lines = open(TSV).read().splitlines(True)
        with open(TSV, "w") as out:
            for line in lines:
                parts = line.rstrip("\n").split("\t", 3)
                if not line.startswith("#") and len(parts) == 4 and parts[0] in decided:
                    verdict, how = decided[parts[0]]
                    if how == "mame":
                        parts[1], parts[2] = verdict, "mame"
                    line = "\t".join(parts) + "\n"
                out.write(line)
        print("classify16k.py: %s updated" % TSV)


if __name__ == "__main__":
    main()
