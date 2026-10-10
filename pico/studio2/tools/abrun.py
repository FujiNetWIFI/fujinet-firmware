#!/usr/bin/env python3
"""abrun.py -- Tier A: every image, frame for frame against MAME's own cart.

    tools/abrun.py [/tmp/studio2 [build/corpus ...]]

Side A is stock MAME (the same binary, the FujiNet device left inert) with
-cart <image>; side B is the device serving the image DIRECT. Both run
emu/abtest.lua: the same keypad script, a snapshot every 2 s. Pass means
every snapshot pair is byte-identical and the last frames are not all the
same picture (the game ran), or MAME refuses the image and so does the cart.
Results go to build/ab/results.tsv; the README carries the table.
"""
import hashlib
import os
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True                  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(__file__))
import st2  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAME = os.environ.get("MAME", os.path.expanduser("~/Workspace/mame-studio2"))
BIN = os.environ.get("MAMEBIN", "./studio2")
ROMS = os.environ.get("ROMS", os.path.expanduser("~/Workspace/mame/roms"))
OUT = os.path.join(HERE, "build", "ab")
SECS = os.environ.get("AB_SECS", "60")

# tools/mkcorpus.py's images, where the cart differs from MAME by design: the
# pages (as testrom/corpus.asm copies them to display RAM) and why.
EXPECTED = {
    "c0c.st2":    ({"0D", "0E", "0F"}, "MAME reads its uninitialised 3K buffer; the cart serves 0"),
    "c0d0f.st2":  ({"0D", "0E"}, "MAME reads its uninitialised 3K buffer; the cart serves 0"),
    "max11.st2":  ({"0A", "0B"}, "MAME never maps $0A00-$0BFF; the cart serves what the image names"),
    "only0a.st2": ({"0A"}, "MAME never maps $0A00; the cart serves what the image names"),
    "only0d.st2": ({"0D"}, "MAME maps $0D00 only with $0C present; the cart serves what the image names"),
    "sub04.st2":  ({"0C", "0D", "0E", "0F"}, "MAME misreads every block after one below $04"),
}
PROBE_ROWS = {0x110: "0A", 0x130: "0B", 0x150: "0C", 0x170: "0D", 0x190: "0E", 0x1B0: "0F"}


def ram_pages(image):
    """The probe pages whose display-RAM bytes differ between the two sides."""
    dumps = []
    for direct in (False, True):
        out = os.path.join(OUT, "ram.%d" % direct)
        env = {k: v for k, v in os.environ.items() if not k.startswith("FUJINET")}
        env["RAMOUT"] = out
        args = [BIN, "studio2", "-rompath", ROMS, "-skip_gameinfo", "-video", "none",
                "-sound", "none", "-nothrottle", "-autoboot_script",
                os.path.join(HERE, "emu", "ramdump.lua"), "-seconds_to_run", "10"]
        if direct:
            env["FUJINET_IMAGE"] = image
        else:
            args += ["-cart", image]
        subprocess.run(args, cwd=MAME, env=env, capture_output=True)
        dumps.append(open(out).read().split())
    a, b = dumps
    return {PROBE_ROWS.get(i & ~7, "RAM%03X" % i) for i in range(512) if a[i] != b[i]}


def side(image, snapdir, direct):
    shutil.rmtree(snapdir, ignore_errors=True)
    os.makedirs(snapdir)
    env = {k: v for k, v in os.environ.items() if not k.startswith("FUJINET")}
    env["AB_SECS"] = SECS
    args = [BIN, "studio2", "-rompath", ROMS, "-snapshot_directory", snapdir,
            "-nvram_directory", os.path.join(OUT, "nvram"), "-cfg_directory", os.path.join(OUT, "cfg"),
            "-skip_gameinfo", "-video", "none", "-sound", "none", "-nothrottle",
            "-autoboot_script", os.path.join(HERE, "emu", "abtest.lua"),
            "-seconds_to_run", str(int(SECS) + 5)]
    if direct:
        env["FUJINET_IMAGE"] = image
    else:
        args += ["-cart", image]
    p = subprocess.run(args, cwd=MAME, env=env, capture_output=True, text=True)
    shots = sorted(os.listdir(os.path.join(snapdir, "studio2"))) if os.path.isdir(
        os.path.join(snapdir, "studio2")) else []
    return p, [hashlib.sha1(open(os.path.join(snapdir, "studio2", s), "rb").read()).hexdigest()
               for s in shots]


def one(image):
    name = os.path.basename(image)
    tag = os.path.join(OUT, name.replace(" ", "_"))
    try:
        st2.parse(open(image, "rb").read())
        refused = False
    except ValueError:
        refused = True
    pa, a = side(image, tag + ".A", False)
    if refused:
        ok = pa.returncode != 0 or "Unsupported cartridge size" in pa.stderr + pa.stdout
        return "SAME" if ok else "FAIL", "refused by the cart; MAME %s" % (
            "refuses it too" if ok else "loads it")
    pb, b = side(image, tag + ".B", True)
    if "fujinet: DIRECT" not in pb.stderr:
        return "FAIL", "the device did not serve it: " + (pb.stderr.strip().splitlines() or ["?"])[-1]
    if not a or len(a) != len(b):
        return "FAIL", "%d snapshots vs %d" % (len(a), len(b))
    diff = [i for i in range(len(a)) if a[i] != b[i]]
    if diff and name in EXPECTED:
        pages, why = EXPECTED[name]
        got = ram_pages(image)
        if got and got <= pages:
            return "EXPECTED", "pages %s differ: %s" % (" ".join("$" + p for p in sorted(got)), why)
        return "DIFF", "pages %s differ, expected only %s" % (sorted(got), sorted(pages))
    if diff:
        return "DIFF", "%d/%d frames differ, first at %d s" % (len(diff), len(a), 2 * (diff[0] + 1))
    alive = len(set(a)) > 1
    return ("EQUAL" if alive else "EQUAL-STATIC"), "%d frames, %d distinct" % (len(a), len(set(a)))


def main():
    roots = [os.path.abspath(r) for r in sys.argv[1:] or ["/tmp/studio2"]]
    os.makedirs(OUT, exist_ok=True)
    images = sorted(os.path.join(d, f) for r in roots for d, _, fs in os.walk(r) for f in fs
                    if f.lower().endswith((".st2", ".bin")))
    rows = []
    for image in images:
        verdict, why = one(image)
        rows.append((os.path.basename(image), verdict, why))
        print("%-12s %s: %s" % (verdict, os.path.basename(image), why), flush=True)
    with open(os.path.join(OUT, "results.tsv"), "w") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")
    bad = [r for r in rows if r[1] in ("FAIL", "DIFF")]
    print("abrun: %d images: %d equal, refused alike or different by design; %d differ"
          % (len(rows), len(rows) - len(bad), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
