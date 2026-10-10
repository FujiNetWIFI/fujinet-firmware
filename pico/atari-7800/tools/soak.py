#!/usr/bin/env python3
"""soak.py -- Tier C: every image over the network, the way CONFIG boots it.

    soak.py [-j N] [--only ID,ID] [--redo] [--direct]

For each image in build/ab/table.tsv (tools/abprep.py): copy it onto the SD
card as /a78soak/cur<k>.bin, run fujiboot built for that path in MAME against
a fujinet-pc of this worker's own, and check:

  - the SRAM the loader filled is byte-equal to the plan's layout (the image
    top-aligned behind $FF for rom kinds, and the $FF page);
  - the hand-over is the one the rule picks (the console's BIOS where it
    passes this BIOS's check, the loader's otherwise);
  - the game is running: the screen 20 s into the game is not blank.

--direct builds fujiboot with BOOT_DIRECT=1 (INPTCTRL locked first), so every
image takes the loader's hand-over: the titles that need the BIOS's own state
show up here. Results: build/soak/results[-direct].tsv, resumable.

Each worker starts a fujinet-pc copy on its own BoIP port (9997 up) sharing
the SD card, and never touches the one on 9995.
"""

import argparse
import concurrent.futures as cf
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "build", "soak")
MAME = os.environ.get("MAME", os.path.expanduser("~/Workspace/mame-a7800"))
MAMEBIN = os.environ.get("MAMEBIN", "./a7800")
ROMS = os.environ.get("ROMS", os.path.expanduser("~/Workspace/mame/roms"))
ROMDIR = os.environ.get("A78_ROMS", "/tmp/atari7800")
FNPC = os.environ.get("FNPC", os.path.expanduser("~/Workspace/fujinet-pc-rs232/build/dist"))
SD = os.path.join(FNPC, "SD")
PAGE = 0x2000

sys.path.insert(0, os.path.join(HERE, "tools"))


def plan_of(data):
    """The cart's plan, from the host planner (tools/a78plan)."""
    p = subprocess.run([os.path.join(OUT, "a78plan")], input=data, capture_output=True)
    f = dict(kv.split("=") for kv in p.stdout.decode().split())
    return {k: int(v, 0) for k, v in f.items()}


def expected_ok(data, plan, dump):
    pages, front, size = plan["pages"], plan["front"], plan["size"]
    want = (b"\xff" * front + data[plan["offset"]:plan["offset"] + size])
    want += b"\xff" * (pages * PAGE - len(want))
    if dump[:pages * PAGE] != want:
        first = next(i for i in range(pages * PAGE) if dump[i] != want[i])
        return "SRAM@%05X" % first
    if dump[63 * PAGE:64 * PAGE] != b"\xff" * PAGE:
        return "FFPAGE"
    return None


class Worker:
    def __init__(self, k, direct):
        self.k = k
        self.port = 9997 + k
        self.dir = os.path.join(OUT, "fnpc%d" % k)
        self.path = "/a78soak/cur%d.bin" % k
        self.cart = os.path.join(OUT, "fujiboot%d.a78" % k)
        env = dict(os.environ, BOOT_HOST="0", BOOT_PATH=self.path,
                   BOOT_DIRECT="1" if direct else "0")
        subprocess.run(["./build.sh", "fujiboot"], cwd=HERE, env=env, check=True,
                       capture_output=True)
        shutil.copy(os.path.join(HERE, "build", "fujiboot.a78"), self.cart)
        shutil.rmtree(self.dir, ignore_errors=True)
        shutil.copytree(FNPC, self.dir, symlinks=True, ignore=shutil.ignore_patterns("SD"))
        os.symlink(SD, os.path.join(self.dir, "SD"))
        ini = os.path.join(self.dir, "fnconfig.ini")
        text = open(ini).read()
        text = re.sub(r"(\[BOIP\][^\[]*?port=)\d+", r"\g<1>%d" % self.port, text, flags=re.S)
        open(ini, "w").write(text)
        self.proc = subprocess.Popen(["./fujinet", "-u", "0.0.0.0:%d" % (8100 + k)],
                                     cwd=self.dir, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(2)

    def stop(self):
        os.killpg(self.proc.pid, signal.SIGTERM)

    def run(self, row, direct):
        rid, f = row[0], row[1]
        system = row[3]
        data = open(os.path.join(ROMDIR, f), "rb").read()
        os.makedirs(os.path.join(SD, "a78soak"), exist_ok=True)
        open(os.path.join(SD, self.path.lstrip("/")), "wb").write(data)
        work = os.path.join(OUT, "work", rid)
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(work)
        env = dict(os.environ, A78_EMU_DIR=os.path.join(HERE, "emu"),
                   FUJINET_TCP="127.0.0.1:%d" % self.port,
                   FUJINET_SRAMDUMP=os.path.join(work, "dump"))
        args = [MAMEBIN, system, "-rompath", ROMS, "-cartslot", "fujinet", "-cart", self.cart,
                "-snapshot_directory", work, "-nvram_directory", os.path.join(work, "nv"),
                "-cfg_directory", os.path.join(work, "cfg"),
                "-autoboot_script", os.path.join(HERE, "emu", "boottest.lua"),
                "-video", "none", "-sound", "none", "-nothrottle", "-seconds_to_run", "150"]
        p = subprocess.run(args, cwd=MAME, env=env, capture_output=True, text=True, timeout=900)
        log = p.stdout + p.stderr
        m = re.search(r"VERDICT (\w+): mode (\d+), hand-over (\w+), screens (\w+) (\w+)", log)
        if not m:
            return "%s\t%s\tNOBOOT\t%s\n" % (rid, f, log.replace("\n", " | ")[-200:])
        alive, ho = m.group(1) == "PASS", m.group(3)
        dump_path = os.path.join(work, "dump.sram")
        if not os.path.exists(dump_path):
            return "%s\t%s\tNODUMP\t%s\n" % (rid, f, ho)
        plan = plan_of(data)
        bad = expected_ok(data, plan, open(dump_path, "rb").read())
        tv_bit = 2 if system == "a7800p" else 1
        want_ho = "bios" if (plan["biosok"] & tv_bit) and not plan["claim"] and not direct else "direct"
        verdict = "PASS"
        if bad:
            verdict = "FAIL-" + bad
        elif ho != want_ho:
            verdict = "FAIL-HANDOVER"
        elif not alive:
            verdict = "FAIL-BLANK"
        shots = sorted(x for x in os.listdir(os.path.join(work, system)) if x.endswith(".png")) \
            if os.path.isdir(os.path.join(work, system)) else []
        if shots:
            os.makedirs(os.path.join(OUT, "shots"), exist_ok=True)
            shutil.copy(os.path.join(work, system, shots[-1]),
                        os.path.join(OUT, "shots", "%s%s.png" % (rid, "-d" if direct else "")))
        shutil.rmtree(work, ignore_errors=True)
        return "%s\t%s\t%s\t%s\t%s\n" % (rid, f, verdict, ho, m.group(0)[8:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--only")
    ap.add_argument("--redo", action="store_true")
    ap.add_argument("--direct", action="store_true")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    subprocess.run(["cc", "-O1", "-I", os.path.join(HERE, "firmware", "include"),
                    "-o", os.path.join(OUT, "a78plan"), os.path.join(HERE, "tools", "a78plan.c"),
                    os.path.join(HERE, "firmware", "src", "a78map.c"),
                    os.path.join(HERE, "firmware", "src", "a78map_db.c")], check=True)
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(HERE, "build", "ab", "table.tsv"))]
    res = os.path.join(OUT, "results-direct.tsv" if a.direct else "results.tsv")
    done = set()
    if os.path.exists(res) and not a.redo:
        done = {l.split("\t")[0] for l in open(res)}
    if a.only:
        want = set(a.only.split(","))
        rows = [r for r in rows if r[0] in want]
    rows = [r for r in rows if r[0] not in done or a.redo or a.only]
    workers = [Worker(k, a.direct) for k in range(a.j)]
    free = list(workers)
    lock = threading.Lock()

    def job(row):
        with lock:
            w = free.pop()
        try:
            return w.run(row, a.direct)
        finally:
            with lock:
                free.append(w)

    try:
        with cf.ThreadPoolExecutor(a.j) as ex, open(res, "a") as out:
            for line in ex.map(job, rows):
                out.write(line)
                out.flush()
                sys.stdout.write(line)
    finally:
        for w in workers:
            w.stop()


if __name__ == "__main__":
    main()
