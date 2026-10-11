#!/usr/bin/env python3
"""soak.py -- Tier C: every image through the real network path.

For each image: fujiboot (rebuilt with its path) mounts it from the live
fujinet-pc ($FUJINET_TCP; its SD holds the images under /studio2), the ESP32
side pushes it, the cart stages it, and the hand-over swaps it in. Pass: the
view the device swapped in is byte-identical to tools/s2plan's for the same
file, and the screen is not blank 10 s and 30 s later.

    tools/soak.py [/tmp/studio2]
"""
import os
import shutil
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True                  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(__file__))
import st2  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/studio2")
OUT = os.path.join(HERE, "build", "soak")
PLAN = os.path.join(HERE, "build", "tools", "s2plan")


def lit(png):
    """True if a snapshot has any lit pixel."""
    data = open(png, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        ln, = struct.unpack(">I", data[pos:pos + 4])
        if data[pos + 4:pos + 8] == b"IDAT":
            idat += data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
    raw = zlib.decompress(idat)
    return any(b > 0x80 for b in raw)


def build_plan():
    os.makedirs(os.path.dirname(PLAN), exist_ok=True)
    fw = os.path.join(HERE, "firmware")
    subprocess.run(["cc", "-O1", "-Wall", "-I" + fw, "-I" + os.path.join(fw, "include"),
                    "-o", PLAN, os.path.join(HERE, "tools", "s2plan.c"),
                    os.path.join(fw, "src", "s2map.c")], check=True)


def one(path, name):
    tag = os.path.join(OUT, name.replace(" ", "_"))
    try:
        st2.parse(open(path, "rb").read())
    except ValueError as e:
        return "SAME", "refused, as MAME refuses it: %s" % e
    want = subprocess.run([PLAN, path], capture_output=True, check=True).stdout
    subprocess.run([os.path.join(HERE, "build.sh"), "fujiboot"], cwd=HERE, check=True,
                   env=dict(os.environ, BOOT_HOST="0", BOOT_PATH="/studio2/" + name),
                   stdout=subprocess.DEVNULL)
    snaps = tag + ".snap"
    shutil.rmtree(snaps, ignore_errors=True)
    dump = tag + ".view"
    for f in os.listdir(OUT):
        if f.startswith(os.path.basename(dump) + "."):
            os.remove(os.path.join(OUT, f))
    env = dict(os.environ, FUJINET_VIEWDUMP=dump, SECS="150")
    p = subprocess.run([os.path.join(HERE, "run.sh"), "fujiboot", "soak"], env=env,
                       capture_output=True, text=True)
    verdict = [l for l in p.stdout.splitlines() if l.startswith("VERDICT")]
    if not verdict or "PASS" not in verdict[-1]:
        return "FAIL", verdict[-1] if verdict else "no verdict"
    got = open(dump + ".0", "rb").read() if os.path.exists(dump + ".0") else b""
    if got != want:
        bad = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), -1)
        return "FAIL", "view differs from s2plan at $%04X" % bad
    shots = sorted(os.path.join(HERE, "build", "snap", "studio2", f)
                   for f in os.listdir(os.path.join(HERE, "build", "snap", "studio2")))[-2:]
    if len(shots) < 2 or not all(lit(s) for s in shots):
        return "FAIL", "blank screen after the swap"
    return "PASS", verdict[-1].split(": ", 1)[1] + "; view = s2plan"


def main():
    os.makedirs(OUT, exist_ok=True)
    build_plan()
    files = sorted(os.path.join(d, f) for d, _, fs in os.walk(CORPUS) for f in fs
                   if f.lower().endswith((".st2", ".bin")))
    rows = []
    for path in files:
        name = os.path.basename(path)
        shutil.rmtree(os.path.join(HERE, "build", "snap"), ignore_errors=True)
        verdict, why = one(path, name)
        rows.append((name, verdict, why))
        print("%-4s %s: %s" % (verdict, name, why), flush=True)
    with open(os.path.join(OUT, "results.tsv"), "w") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")
    bad = [r for r in rows if r[1] == "FAIL"]
    print("soak: %d/%d pass or refused alike" % (len(rows) - len(bad), len(rows)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
