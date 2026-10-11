#!/usr/bin/env python3
"""tierb.py -- Tier B: a hand-over leaves the machine as a power-on does.

    tierb.py [-j N] [--only ID,ID] [--redo]

For each image in build/ab/table.tsv, at the BIOS's first read of $BFFD:
once with the image served from power-on (the device's DIRECT mode), once
after fujiboot pushed it and the cart's stub swapped it in. RAM $0000-$3FFF
must match, and so must SP, X and the flags. A, Y and the chips' timers are
not compared: the BIOS sets A and Y before using them, and a hand-over
cannot fix a timer's phase.
Results: build/soak/results-tierb.tsv, resumable. Workers' fujinet-pc
copies listen from port 9930.
"""

import argparse
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, "tools"))
import soak  # noqa: E402

OUT = soak.OUT


def mame(work, env, cart):
    args = [soak.MAMEBIN, "a5200", "-rompath", soak.ROMS, "-skip_gameinfo", "-cartslot", "fujinet"]
    if cart:
        args += ["-cart", cart]
    args += ["-nvram_directory", os.path.join(work, "nv"), "-cfg_directory", os.path.join(work, "cfg"),
             "-snapshot_directory", work,
             "-autoboot_script", os.path.join(HERE, "emu", "handover.lua"),
             "-video", "none", "-sound", "none", "-nothrottle", "-seconds_to_run", "120"]
    p = subprocess.run(args, cwd=soak.MAME, env=env, capture_output=True, text=True, timeout=600)
    m = re.search(r"HO \w+ SP=(\w+) X=(\w+) P=(\w+)", p.stdout + p.stderr)
    return (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16)) if m else None


def one(w, row):
    rid, f = row[0], row[1]
    src = os.path.join(soak.ROMDIR, f)
    work = os.path.join(OUT, "tierb", rid)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    base = dict(os.environ, A52_EMU_DIR=os.path.join(HERE, "emu"))
    for k in ("FUJINET_IMAGE", "FUJINET_MAPPER", "FUJINET_MERGE", "FUJINET_VIEWDUMP"):
        base.pop(k, None)
    na = mame(work, dict(base, HO_MODE="native", HO_OUT=os.path.join(work, "native.ram"),
                         FUJINET_IMAGE=src), None)
    shutil.copy(src, os.path.join(soak.SD, w.path.lstrip("/")))
    nb = mame(work, dict(base, HO_MODE="net", HO_OUT=os.path.join(work, "net.ram"),
                         FUJINET_TCP="127.0.0.1:%d" % w.port), w.cart)
    if not na or not nb:
        return "%s\t%s\tNORUN\t%s %s\n" % (rid, f, na, nb)
    ra = open(os.path.join(work, "native.ram"), "rb").read()
    rb = open(os.path.join(work, "net.ram"), "rb").read()
    diff = [a for a in range(len(ra)) if ra[a] != rb[a]]
    verdict = "PASS"
    if diff:
        verdict = "FAIL-RAM@%04X(%d)" % (diff[0], len(diff))
    elif na[0] != nb[0] or na[1] != nb[1] or (na[2] ^ nb[2]) & 0xCF:
        verdict = "FAIL-REGS"
    shutil.rmtree(work, ignore_errors=True)
    return "%s\t%s\t%s\tnative SP=%02X X=%02X P=%02X; net SP=%02X X=%02X P=%02X\n" % (
        (rid, f, verdict) + na + nb)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=8)
    ap.add_argument("--only")
    ap.add_argument("--redo", action="store_true")
    a = ap.parse_args()
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(HERE, "build", "ab", "table.tsv"))]
    res = os.path.join(OUT, "results-tierb.tsv")
    done = set()
    if os.path.exists(res) and not a.redo:
        done = {l.split("\t")[0] for l in open(res)}
    if a.only:
        want = set(a.only.split(","))
        rows = [r for r in rows if r[0] in want]
    rows = [r for r in rows if r[0] not in done or a.redo or a.only]
    if not rows:
        return
    workers = [soak.Worker(k, base=9930) for k in range(min(a.j, len(rows)))]
    free = list(workers)
    lock = threading.Lock()

    def job(row):
        with lock:
            w = free.pop()
        try:
            return one(w, row)
        finally:
            with lock:
                free.append(w)

    try:
        with cf.ThreadPoolExecutor(len(workers)) as ex, open(res, "a") as out:
            for line in ex.map(job, rows):
                out.write(line)
                out.flush()
                sys.stdout.write(line)
    finally:
        for w in workers:
            w.stop()


if __name__ == "__main__":
    main()
