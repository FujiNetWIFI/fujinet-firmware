#!/usr/bin/env python3
"""cmpfw.py -- build the cartridge firmwares at two refs and compare the binaries.

    pico/tools/cmpfw.py [-b BASE] [-a AFTER | -w] [cart ...]

BASE defaults to the merge base of origin/master and HEAD, AFTER to HEAD; -w takes the
working tree instead. Both sides build from the same path under build/cmpfw/ with no build
date, so a change that only moves code gives identical .bin files. Where they differ, the
functions whose code differs are listed. Exit status: 0 identical, 1 different, 2 failed.
"""

import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile

CARTS = ["arcadia", "astrocade", "channelf", "nes", "sms", "studio2",
         "atari-5200", "atari-7800", "s100", "o2"]

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], check=True,
                      capture_output=True, text=True).stdout.strip()
WORK = os.path.join(ROOT, "build", "cmpfw")
SRC = os.path.join(WORK, "src")
OBJ = os.path.join(WORK, "obj")


def die(msg):
    print("cmpfw: " + msg, file=sys.stderr)
    sys.exit(2)


def git(*args, binary=False):
    return subprocess.run(["git", "-C", ROOT, *args], check=True,
                          capture_output=True, text=not binary).stdout


def extract(ref):
    shutil.rmtree(SRC, ignore_errors=True)
    os.makedirs(SRC)
    if ref is None:
        listed = git("ls-files", "-co", "--exclude-standard", "-z", "--", "pico")
        for path in listed.split("\0"):
            if path and os.path.isfile(os.path.join(ROOT, path)):
                dst = os.path.join(SRC, path)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(os.path.join(ROOT, path), dst)
    else:
        tar = io.BytesIO(git("archive", ref, "pico", binary=True))
        tarfile.open(fileobj=tar).extractall(SRC, filter="data")
    # Ignored generated headers (channelf, nes) come from this checkout, same on both sides.
    for path in git("ls-files", "-oi", "--exclude-standard", "--",
                    "pico/*/firmware/include/*.h").splitlines():
        dst = os.path.join(SRC, path)
        if os.path.isdir(os.path.dirname(dst)) and not os.path.exists(dst):
            shutil.copy2(os.path.join(ROOT, path), dst)


def build(cart, side):
    src = os.path.join(SRC, "pico", cart, "firmware")
    if not os.path.isfile(os.path.join(src, "CMakeLists.txt")):
        return None
    obj = os.path.join(OBJ, cart)
    out = os.path.join(WORK, side)
    os.makedirs(out, exist_ok=True)
    shutil.rmtree(obj, ignore_errors=True)
    env = dict(os.environ, CFLAGS="-DPICO_NO_BI_PROGRAM_BUILD_DATE=1",
               CXXFLAGS="-DPICO_NO_BI_PROGRAM_BUILD_DATE=1")
    env.setdefault("PICO_SDK_PATH", "/usr/share/pico-sdk")
    log = os.path.join(out, cart + ".log")
    with open(log, "w") as f:
        for cmd in (["cmake", "-S", src, "-B", obj, "-G", "Ninja",
                     "-DCMAKE_BUILD_TYPE=Release", "-DFUJI_GEN=" + os.path.join(SRC, "none")],
                    ["ninja", "-C", obj]):
            if subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, env=env).returncode:
                die("%s failed at %s; see %s" % (cart, side, log))
    elf = [n for n in os.listdir(obj) if n.endswith(".elf")]
    if len(elf) != 1:
        die("%s: expected one .elf in %s, found %s" % (cart, obj, elf))
    stem = os.path.join(obj, elf[0][:-4])
    for ext in (".elf", ".bin"):
        shutil.copy2(stem + ext, os.path.join(out, cart + ext))
    return os.path.join(out, cart)


def functions(elf):
    """Each function's instructions, with flash and RAM addresses masked so a move alone
    compares equal."""
    funcs = set()
    for line in subprocess.run(["arm-none-eabi-readelf", "-sW", elf], check=True,
                               capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if len(f) == 8 and f[3] == "FUNC":
            funcs.add(f[7])
    out, name, seen = {}, None, {}
    for line in subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", elf],
                               check=True, capture_output=True, text=True).stdout.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            sym = m.group(1)
            if sym not in funcs:
                name = None
                continue
            seen[sym] = seen.get(sym, 0) + 1
            name = "%s#%d" % (sym, seen[sym]) if seen[sym] > 1 else sym
            out[name] = []
        elif name and re.match(r"^\s*[0-9a-f]+:\t", line):
            body = line.split(":", 1)[1]
            out[name].append(re.sub(r"\b(0x)?[12][0-9a-f]{7}\b", "X", body).strip())
    return out


def compare(cart, before, after):
    if before is None or after is None:
        return "only %s" % ("after" if before is None else "before"), False
    if open(before + ".bin", "rb").read() == open(after + ".bin", "rb").read():
        return "identical", True
    fb, fa = functions(before + ".elf"), functions(after + ".elf")
    changed = sorted(n for n in fb.keys() & fa.keys() if fb[n] != fa[n])
    gone, new = sorted(fb.keys() - fa.keys()), sorted(fa.keys() - fb.keys())
    if not (changed or gone or new):
        return "differs in data or layout only; every function's code is the same", False
    core1 = [n for n in changed + gone + new if "core1" in n]
    lines = ["differs%s" % ("  ** core1: %s **" % " ".join(core1) if core1 else "")]
    for label, names in (("changed", changed), ("only before", gone), ("only after", new)):
        if names:
            lines.append("    %-12s %s" % (label, " ".join(names)))
    return "\n".join(lines), False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-b", "--base",
                    help="before ref (default: merge base of origin/master and HEAD)")
    ap.add_argument("-a", "--after", default="HEAD", help="after ref (default: HEAD)")
    ap.add_argument("-w", "--worktree", action="store_true", help="after = the working tree")
    ap.add_argument("carts", nargs="*", default=CARTS)
    args = ap.parse_args()
    base = args.base or git("merge-base", "origin/master", "HEAD").strip()
    after = None if args.worktree else args.after

    built = {}
    for side, ref in (("before", base), ("after", after)):
        print("cmpfw: building %s (%s)" % (side, ref or "working tree"), flush=True)
        extract(ref)
        built[side] = {c: build(c, side) for c in args.carts}

    same = True
    for cart in args.carts:
        verdict, ok = compare(cart, built["before"][cart], built["after"][cart])
        same &= ok
        print("%-11s %s" % (cart, verdict))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
