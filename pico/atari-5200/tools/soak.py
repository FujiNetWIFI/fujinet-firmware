#!/usr/bin/env python3
"""soak.py -- Tier C: every image over the network, the way CONFIG boots it.

    soak.py [-j N] [--only ID,ID] [--redo] [--merge]

For each image in build/ab/table.tsv (tools/abprep.py): copy it onto the SD
card as /a52soak/cur<port>.bin, run fujiboot built for that path in MAME against
a fujinet-pc of this worker's own, and check:

  - the cart swapped it in through the stub;
  - the 32K window it serves at the swap is byte-equal to the planner's
    (a52plan --view: the host build of the same a52map.c);
  - the game is running: the screen 10 s and 30 s in is not one colour.

--merge runs the device with FUJINET_MERGE=1, as a 4-port console's
clockless cart would see the bus. Results: build/soak/results[-merge].tsv,
resumable.

Each worker starts a fujinet-pc copy on its own BoIP port (9960 up) sharing
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

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "build", "soak")
MAME = os.environ.get("MAME", os.path.expanduser("~/Workspace/mame-a5200"))
MAMEBIN = os.environ.get("MAMEBIN", "./a5200")
ROMS = os.environ.get("ROMS", os.path.expanduser("~/Workspace/mame/roms"))
ROMDIR = os.environ.get("A52_ROMS", "/tmp/a5200")
FNPC = os.environ.get("FNPC", os.path.expanduser("~/Workspace/fujinet-pc-rs232/build/dist"))
SD = os.path.join(FNPC, "SD")
A52PLAN = os.path.join(HERE, "build", "tools", "a52plan")


class Worker:
    def __init__(self, k, base=9960):
        self.k = k
        self.port = base + k
        self.dir = os.path.join(OUT, "fnpc%d" % self.port)
        self.path = "/a52soak/cur%d.bin" % self.port
        self.cart = os.path.join(OUT, "fujiboot%d.bin" % self.port)
        env = dict(os.environ, BOOT_HOST="0", BOOT_PATH=self.path)
        subprocess.run(["./build.sh", "fujiboot"], cwd=HERE, env=env, check=True,
                       capture_output=True)
        shutil.copy(os.path.join(HERE, "build", "fujiboot.bin"), self.cart)
        shutil.rmtree(self.dir, ignore_errors=True)
        shutil.copytree(FNPC, self.dir, symlinks=True, ignore=shutil.ignore_patterns("SD"))
        os.symlink(SD, os.path.join(self.dir, "SD"))
        ini = os.path.join(self.dir, "fnconfig.ini")
        text = open(ini).read()
        text = re.sub(r"(\[BOIP\][^\[]*?port=)\d+", r"\g<1>%d" % self.port, text, flags=re.S)
        open(ini, "w").write(text)
        self.proc = subprocess.Popen(["./fujinet", "-u", "0.0.0.0:%d" % (self.port - 1800)],
                                     cwd=self.dir, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(2)

    def stop(self):
        os.killpg(self.proc.pid, signal.SIGTERM)

    def run(self, row, merge):
        rid, f, system = row[0], row[1], row[3]
        src = os.path.join(ROMDIR, f)
        os.makedirs(os.path.join(SD, "a52soak"), exist_ok=True)
        shutil.copy(src, os.path.join(SD, self.path.lstrip("/")))
        work = os.path.join(OUT, "work", rid)
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(work)
        env = dict(os.environ, A52_EMU_DIR=os.path.join(HERE, "emu"),
                   FUJINET_TCP="127.0.0.1:%d" % self.port,
                   FUJINET_VIEWDUMP=os.path.join(work, "view.bin"))
        env.pop("FUJINET_IMAGE", None)
        env.pop("FUJINET_MAPPER", None)
        if merge:
            env["FUJINET_MERGE"] = "1"
        args = [MAMEBIN, system, "-rompath", ROMS, "-skip_gameinfo",
                "-cartslot", "fujinet", "-cart", self.cart,
                "-snapshot_directory", work, "-nvram_directory", os.path.join(work, "nv"),
                "-cfg_directory", os.path.join(work, "cfg"),
                "-autoboot_script", os.path.join(HERE, "emu", "boottest.lua"),
                "-video", "none", "-sound", "none", "-nothrottle", "-seconds_to_run", "150"]
        p = subprocess.run(args, cwd=MAME, env=env, capture_output=True, text=True, timeout=900)
        log = p.stdout + p.stderr
        m = re.search(r"VERDICT (\w+): (.*)", log)
        if not m:
            return "%s\t%s\tNOBOOT\t%s\n" % (rid, f, log.replace("\n", " | ")[-200:])
        detail = m.group(2)
        if not detail.startswith("swap"):
            return "%s\t%s\tNOSWAP\t%s\n" % (rid, f, detail.replace("\n", " | ")[-200:])
        view = os.path.join(work, "view.bin")
        want = os.path.join(work, "want.bin")
        subprocess.run([A52PLAN, "--view", want, src], capture_output=True,
                       env=dict(os.environ, FUJINET_MAPPER=""))
        verdict = "PASS"
        if not os.path.exists(view):
            verdict = "FAIL-NODUMP"
        else:
            got, exp = open(view, "rb").read(), open(want, "rb").read()
            if got != exp:
                first = next((i for i in range(min(len(got), len(exp))) if got[i] != exp[i]),
                             min(len(got), len(exp)))
                verdict = "FAIL-VIEW@%04X" % (0x4000 + first)
            elif m.group(1) != "PASS":
                verdict = "FAIL-BLANK"
        shots = sorted(x for x in os.listdir(os.path.join(work, system)) if x.endswith(".png")) \
            if os.path.isdir(os.path.join(work, system)) else []
        if shots:
            os.makedirs(os.path.join(OUT, "shots"), exist_ok=True)
            shutil.copy(os.path.join(work, system, shots[-1]),
                        os.path.join(OUT, "shots", "%s%s.png" % (rid, "-m" if merge else "")))
        shutil.rmtree(work, ignore_errors=True)
        return "%s\t%s\t%s\t%s\n" % (rid, f, verdict, detail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=8)
    ap.add_argument("--only")
    ap.add_argument("--redo", action="store_true")
    ap.add_argument("--merge", action="store_true")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(os.path.dirname(A52PLAN), exist_ok=True)
    subprocess.run(["cc", "-O1", "-I", os.path.join(HERE, "firmware", "include"),
                    "-o", A52PLAN, os.path.join(HERE, "tools", "a52plan.c"),
                    os.path.join(HERE, "firmware", "src", "a52map.c"),
                    os.path.join(HERE, "firmware", "src", "a52map_db.c")], check=True)
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(HERE, "build", "ab", "table.tsv"))]
    res = os.path.join(OUT, "results-merge.tsv" if a.merge else "results.tsv")
    done = set()
    if os.path.exists(res) and not a.redo:
        done = {l.split("\t")[0] for l in open(res)}
    if a.only:
        want = set(a.only.split(","))
        rows = [r for r in rows if r[0] in want]
    rows = [r for r in rows if r[0] not in done or a.redo or a.only]
    if not rows:
        return
    workers = [Worker(k) for k in range(min(a.j, len(rows)))]
    free = list(workers)
    lock = threading.Lock()

    def job(row):
        with lock:
            w = free.pop()
        try:
            return w.run(row, a.merge)
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
