#!/usr/bin/env python3
"""abrun.py -- Tier A: every image on stock MAME's own cart (A) and on the
FujiNet cart serving it from power-on (B), the same console and its real
BIOS, the same inputs; the snapshots must match.

    abrun.py [-j N] [--only ID,ID] [--redo] [--system a5200a]

Reads build/ab/table.tsv (tools/abprep.py). Results go to
build/ab/results-<system>.tsv, one line per image, so a rerun only does what
is missing. The last snapshot of each B run is kept as
build/ab/shots/<id>.png for the contact sheet.
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
MAME = os.environ.get("MAME", os.path.expanduser("~/Workspace/mame-a5200"))
MAMEBIN = os.environ.get("MAMEBIN", "./a5200")
ROMS = os.environ.get("ROMS", os.path.expanduser("~/Workspace/mame/roms"))
ROMDIR = os.environ.get("A52_ROMS", "/tmp/a5200")
SYSTEM = None                           # --system: the console for both sides


def run(side, row):
    rid, f, a, system, note = row
    system = SYSTEM or system
    work = os.path.join(AB, "work", "%s-%s" % (rid, side))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    args = [MAMEBIN, system, "-skip_gameinfo",
            "-rompath", "%s;%s" % (os.path.join(AB, "roms"), ROMS),
            "-snapshot_directory", work, "-nvram_directory", os.path.join(work, "nvram"),
            "-cfg_directory", os.path.join(work, "cfg"),
            "-autoboot_script", os.path.join(HERE, "emu", "abtest.lua"),
            "-video", "none", "-sound", "none", "-nothrottle", "-seconds_to_run", "90"]
    env = dict(os.environ, AB_EVERY="120")
    if side == "B":
        env["FUJINET_IMAGE"] = os.path.join(ROMDIR, f)
        args[2:2] = ["-cartslot", "fujinet"]
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
    ap.add_argument("--system", default="a5200")
    a = ap.parse_args()
    global SYSTEM
    SYSTEM = a.system
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(AB, "table.tsv"))]
    res = os.path.join(AB, "results-%s.tsv" % a.system)
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
