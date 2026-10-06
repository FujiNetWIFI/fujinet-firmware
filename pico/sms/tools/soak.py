#!/usr/bin/env python3
"""soak.py -- Tier C: every image through the real path. The BIOS boots
fujiboot from the cart; fujiboot mounts the image from fujinet-pc's SD, the
ESP32 side pushes it, the loader copies it into the SRAM, the cart flips.

    soak.py [--only ID,ID] [--redo]

Pass: the cart's SRAM holds the image byte for byte (and Janggun's reversed
copy), the cart planned it as the host planner does, and the game is on screen
after the flip: at +10 s and +30 s, or with SOAK_EVERY=n in any of the
snapshots taken every n seconds up to +30 s. fujinet-pc takes one client: this runs serially.
Results: build/soak/results.tsv; shots: build/soak/shots/<id>-{1,2}.png.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(HERE, "build")
SOAK = os.path.join(B, "soak")
MAME = os.environ.get("MAME", os.path.expanduser("~/Workspace/mame"))
ROMDIR = os.environ.get("SMS_ROMS", "/tmp/SMS")
SD = os.environ.get("SOAK_SD", os.path.expanduser("~/Workspace/fujinet-pc-rs232/build/dist/SD"))


def planner():
    exe = os.path.join(B, "smsplan")
    fw = os.path.join(HERE, "firmware")
    subprocess.run(["cc", "-O1", "-I" + os.path.join(fw, "include"), "-o", exe,
                    os.path.join(HERE, "tools", "smsplan.c"), os.path.join(fw, "src", "smsmap.c"),
                    os.path.join(fw, "src", "smsmap_db.c")], check=True)
    return exe


def fujiboot():
    """build/fujiboot.sms, rebuilt if it boots anything but the soak image."""
    path = os.path.join(B, "fujiboot.sms")
    if not os.path.exists(path) or b"/smsoak/cur.bin" not in open(path, "rb").read():
        subprocess.run([os.path.join(HERE, "build.sh")], cwd=HERE, check=True,
                       env=dict(os.environ, BOOT_PATH="/smsoak/cur.bin"))
    return path


def bitrev(data):
    t = bytes(int("{:08b}".format(i)[::-1], 2) for i in range(256))
    return data.translate(t)


def one(row, plan_exe):
    rid, f, name, system, note = row[:5]
    src = os.path.join(ROMDIR, f)
    data = open(src, "rb").read()
    os.makedirs(os.path.join(SD, "smsoak"), exist_ok=True)
    shutil.copy(src, os.path.join(SD, "smsoak", "cur.bin"))
    work = os.path.join(SOAK, "work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    dump = os.path.join(work, "cur")
    system = "sms1pal" if system == "smspal" else "sms1"
    args = ["./mame", system, "-slot", "fujinet", "-cart", fujiboot(),
            "-skip_gameinfo", "-snapshot_directory", work,
            "-nvram_directory", os.path.join(work, "nv"), "-cfg_directory", os.path.join(work, "cfg"),
            "-autoboot_script", os.path.join(HERE, "emu", "soak.lua"),
            "-video", "none", "-sound", "none", "-nothrottle", "-seconds_to_run", "160"]
    env = dict(os.environ, FUJINET_SRAMDUMP=dump, SMS_EMU_DIR=os.path.join(HERE, "emu"))
    p = subprocess.run(args, cwd=MAME, env=env, capture_output=True, text=True, timeout=900)
    log = p.stdout + p.stderr

    want = subprocess.run([plan_exe, src], capture_output=True, text=True).stdout.split()
    got = re.search(r"fujinet: loaded (kind=\S+ crc=\S+ size=\S+ ram=\S+ claim=\S+)", log)
    flip = re.search(r"SOAK flipped ([\d.]+)", log)
    verdict = []
    if not got:
        verdict.append("NOLOAD")
    elif got.group(1) != " ".join(want[1:]):
        verdict.append("PLAN(%s)" % got.group(1))
    if os.path.exists(dump + ".sram"):
        sram = open(dump + ".sram", "rb").read()
        off = 512 if len(data) % 0x4000 == 512 else 0
        img = data[off:]
        padded = img + b"\0" * ((-len(img)) % 0x4000)
        expect = padded + (bitrev(padded) if "kind=janggun" in " ".join(want) else b"")
        if sram != expect:
            verdict.append("SRAM")
    elif got:
        verdict.append("NODUMP")
    shots = sorted(os.path.join(r, x) for r, _, fs in os.walk(work) for x in fs if x.endswith(".png"))
    os.makedirs(os.path.join(SOAK, "shots"), exist_ok=True)
    best = sorted(shots, key=os.path.getsize)[-1:] if shots else []
    for i, s in enumerate((shots[:1] + best) if len(shots) > 2 else shots[:2]):
        shutil.copy(s, os.path.join(SOAK, "shots", "%s-%d.png" % (rid, i + 1)))
    if not flip:
        verdict.append("NOFLIP")
    elif len(shots) < 2:
        verdict.append("NOSHOTS")
    elif all(os.path.getsize(x) < 400 for x in shots):
        verdict.append("BLANK")
    if "SOAK FAIL" in log:
        verdict.append("BOOTFAIL")
    v = ",".join(verdict) if verdict else "PASS"
    tail = "" if v == "PASS" else log[-300:].replace("\n", " | ")
    return "%s\t%s\t%s\t%s\t%s\t%s\n" % (rid, f, v, flip.group(1) if flip else "-",
                                         got.group(1) if got else "-", tail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--redo", action="store_true")
    a = ap.parse_args()
    os.makedirs(SOAK, exist_ok=True)
    exe = planner()
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(B, "ab", "table.tsv"))]
    res = os.path.join(SOAK, "results.tsv")
    done = set()
    if os.path.exists(res) and not a.redo:
        done = {l.split("\t")[0] for l in open(res)}
    if a.only:
        want = set(a.only.split(","))
        rows = [r for r in rows if r[0] in want]
    with open(res, "a") as out:
        for r in rows:
            if r[0] in done and not (a.redo or a.only):
                continue
            line = one(r, exe)
            out.write(line)
            out.flush()
            sys.stdout.write(line)


if __name__ == "__main__":
    main()
