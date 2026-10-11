#!/usr/bin/env python3
"""tierb.py -- Tier B: does a game find the machine as a power-on leaves it?

For each image: run it DIRECT from power-on, then pushed and handed over by
fujiboot through the live fujinet-pc ($FUJINET_TCP, its SD holding the images
under /studio2), and compare emu/handover.lua's dumps at the game's first read
of $0400-$07FF.

    tools/tierb.py [/tmp/studio2]

Compared: every register the BIOS sets or reads before it reaches the cart
(R1 R2 R4 R5 R6 R7 R8 R10 R12 R15 X P IE) and the RAM, except the bytes the
hand-over cannot match by design: Q and the beep timer $08CD-$08CF (the BIOS
ISR's beep runs from a different frame phase), and $08BB-$08BF below the
BIOS's stack pointer.
"""
import os
import subprocess
import sys

sys.dont_write_bytecode = True                  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(__file__))
import st2  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/studio2")
OUT = os.path.join(HERE, "build", "tierb")
REGS = ("R1", "R2", "R4", "R5", "R6", "R7", "R8", "R10", "R12", "R15", "X", "P", "IE")
SKIP_RAM = set(range(0x0BB, 0x0C0)) | set(range(0x0CD, 0x0D0))


def run(env_extra, client, out):
    env = dict(os.environ, TIERB_OUT=out, SECS="60", **env_extra)
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([os.path.join(HERE, "run.sh"), client, "handover"], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if not os.path.exists(out):
        return None
    return dict(line.split() for line in open(out))


def main():
    os.makedirs(OUT, exist_ok=True)
    files = sorted(os.path.join(d, f) for d, _, fs in os.walk(CORPUS) for f in fs
                   if f.lower().endswith((".st2", ".bin")))
    results = []
    for path in files:
        name = os.path.basename(path)
        tag = os.path.join(OUT, name.replace(" ", "_"))
        try:
            st2.parse(open(path, "rb").read())
        except ValueError as e:
            # the cart refuses it at the push, as MAME refuses it at -cart
            results.append((name, "SAME", "refused, as MAME refuses it: %s" % e))
            print("%-4s %s: %s" % (results[-1][1], name, results[-1][2]), flush=True)
            continue
        native = run({"IMAGE": path}, "-", tag + ".native")
        subprocess.run([os.path.join(HERE, "build.sh"), "fujiboot"], cwd=HERE, check=True,
                       env=dict(os.environ, BOOT_HOST="0", BOOT_PATH="/studio2/" + name),
                       stdout=subprocess.DEVNULL)
        hand = run({}, "fujiboot", tag + ".hand")
        if native is None and hand is None:
            results.append((name, "FAIL", "the trigger never fired on either side"))
            print("%-4s %s: %s" % (results[-1][1], name, results[-1][2]), flush=True)
            continue
        if native is None or hand is None:
            results.append((name, "FAIL", "native %s, hand-over %s"
                            % ("ok" if native else "never", "ok" if hand else "never")))
            print("%-4s %s: %s" % (results[-1][1], name, results[-1][2]), flush=True)
            continue
        diff = [r for r in REGS if native[r] != hand[r]]
        diff += ["RAM%03X" % i for i in range(512) if i not in SKIP_RAM
                 and native["RAM%03X" % i] != hand["RAM%03X" % i]]
        if diff:
            shown = ", ".join("%s %s/%s" % (k, native[k], hand[k]) for k in diff[:8])
            results.append((name, "FAIL", "%d differ: %s" % (len(diff), shown)))
        else:
            results.append((name, "PASS", "reached at %.3f s native, %.3f s via the hand-over"
                            % (float(native["TIME"]), float(hand["TIME"]))))
        print("%-4s %s: %s" % (results[-1][1], name, results[-1][2]), flush=True)
    fails = [r for r in results if r[1] == "FAIL"]
    print("tierb: %d/%d equal" % (len(results) - len(fails), len(results)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
