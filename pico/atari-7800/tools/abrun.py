#!/usr/bin/env python3
"""abrun.py -- Tier A: every image on stock MAME's own cart (A) and on the
FujiNet cart (B), the same console and its real BIOS, the same inputs; the
snapshots must match.

    abrun.py [-j N] [--only ID,ID] [--redo] [--hsc ROM]

Reads build/ab/table.tsv (tools/abprep.py). Results go to build/ab/results.tsv
(results-hsc.tsv with --hsc), one line per image, so a rerun only does what is
missing. The last snapshot of each B run is kept as build/ab/shots/<id>.png
for the contact sheet. With --hsc, side A is MAME's High Score Cart with the
game in its pass-through slot, side B the FujiNet cart with the HSC on.
"""

import argparse
import concurrent.futures as cf
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AB = os.path.join(HERE, "build", "ab")
MAME = os.environ.get("MAME", os.path.expanduser("~/Workspace/mame-a7800"))
MAMEBIN = os.environ.get("MAMEBIN", "./a7800")
ROMS = os.environ.get("ROMS", os.path.expanduser("~/Workspace/mame/roms"))
ROMDIR = os.environ.get("A78_ROMS", "/tmp/atari7800")
HSC = None                              # --hsc: the HSC ROM, on both sides


def run(side, row):
    rid, f, a, system, note = row
    work = os.path.join(AB, "work", "%s-%s" % (rid, side))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    args = [MAMEBIN, system, "-skip_gameinfo",
            "-rompath", "%s;%s" % (os.path.join(AB, "roms"), ROMS),
            "-snapshot_directory", work, "-nvram_directory", os.path.join(work, "nvram"),
            "-cfg_directory", os.path.join(work, "cfg"),
            "-autoboot_script", os.path.join(HERE, "emu", "abtest.lua"),
            "-video", "none", "-sound", "none", "-nothrottle", "-seconds_to_run", "90"]
    env = dict(os.environ, AB_EVERY="100" if system == "a7800p" else "120")
    if side == "B":
        env["FUJINET_IMAGE"] = os.path.join(ROMDIR, f)
        if HSC:
            env["FUJINET_HSC"] = HSC
        args[2:2] = ["-cartslot", "fujinet", "-cart", os.path.join(HERE, "build", "hello.a78")]
    elif HSC and a != "hiscore":
        # MAME's HSC from its software list, the game in its pass-through slot
        args[2:2] = ["-cart", "hiscore", "-cart2", a]
    elif a == "hiscore":
        args.insert(2, "hiscore")
    else:
        args[2:2] = ["-cart", a]
    p = subprocess.run(args, cwd=MAME, env=env, capture_output=True, text=True, timeout=600)
    shots = []
    for root, _, files in os.walk(work):
        for x in files:
            if x.endswith(".png"):
                shots.append(os.path.join(root, x))
    shots.sort()
    return work, shots, p.returncode, (p.stdout + p.stderr)[-400:]


def digest(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def blank(path):
    # a PNG of one colour compresses to almost nothing
    return os.path.getsize(path) < 400


def one(row):
    rid = row[0]
    wa, sa, ra, la = run("A", row)
    wb, sb, rb, lb = run("B", row)
    ha = [digest(p) for p in sa]
    hb = [digest(p) for p in sb]
    if sb:
        os.makedirs(os.path.join(AB, "shots"), exist_ok=True)
        shutil.copy(sb[-1], os.path.join(AB, "shots", "%s.png" % rid))
    if not ha:
        verdict = "A-NOLOAD"
    elif not hb:
        verdict = "B-NOLOAD"
    elif ha == hb:
        distinct = len(set(hb))
        if distinct < 2:
            verdict = "EQUAL-STATIC"
        elif all(blank(p) for p in sb[-5:]):
            verdict = "EQUAL-BLANK"
        else:
            verdict = "EQUAL"
    else:
        first = next(i for i, (x, y) in enumerate(zip(ha, hb)) if x != y) if len(ha) == len(hb) else -1
        verdict = "DIFF@%d" % first
    shutil.rmtree(wa, ignore_errors=True)
    shutil.rmtree(wb, ignore_errors=True)
    tail = "" if verdict.startswith("EQUAL") else (la if not ha else lb).replace("\n", " | ")[-200:]
    return "%s\t%s\t%s\t%d/%d\t%s\n" % (rid, row[1], verdict, len(ha), len(hb), tail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=max(1, os.cpu_count() // 2))
    ap.add_argument("--only")
    ap.add_argument("--redo", action="store_true")
    ap.add_argument("--hsc")
    a = ap.parse_args()
    global HSC
    HSC = a.hsc
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(AB, "table.tsv"))]
    res = os.path.join(AB, "results-hsc.tsv" if a.hsc else "results.tsv")
    done = set()
    if os.path.exists(res) and not a.redo:
        done = {l.split("\t")[0] for l in open(res)}
    if a.only:
        want = set(a.only.split(","))
        rows = [r for r in rows if r[0] in want]
    rows = [r for r in rows if r[0] not in done or a.redo or a.only]
    with cf.ThreadPoolExecutor(a.j) as ex, open(res, "a") as out:
        for line in ex.map(one, rows):
            out.write(line)
            out.flush()
            sys.stdout.write(line)


if __name__ == "__main__":
    main()
