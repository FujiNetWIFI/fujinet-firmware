#!/usr/bin/env python3
"""checksram.py -- core1's loop, and everything it calls, must live in SRAM.

core0 runs from flash; core1 must never take an XIP miss mid-read. This
disassembles a52_core1_main and fails on any call or branch that leaves SRAM,
and on any shared state the loop touches that is not in SRAM.

Usage: checksram.py fuji5200.elf
"""

import re
import subprocess
import sys

SRAM_LO, SRAM_HI = 0x20000000, 0x20082000
ENTRY = "a52_core1_main"
DATA = ("fuji_loader", "fuji_arena", "fuji_ring")


def main():
    elf = sys.argv[1]
    nm = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True,
                        check=True).stdout
    syms = {}
    for line in nm.splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    bad = []
    for name in (ENTRY,) + DATA:
        if name not in syms:
            bad.append("%s is not in the image" % name)
        elif not SRAM_LO <= syms[name] < SRAM_HI:
            bad.append("%s is at %#010x -- not in SRAM" % (name, syms[name]))
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn",
                          "--disassemble=" + ENTRY, elf],
                         capture_output=True, text=True, check=True).stdout
    calls = 0
    for line in dis.splitlines():
        m = re.search(r"\s(bl|blx|b\.?\w*|cb\w+)\s+([0-9a-f]+)\s*<([^>]+)>", line)
        if m:
            calls += 1
            tgt = int(m.group(2), 16)
            if not SRAM_LO <= tgt < SRAM_HI:
                bad.append("%s branches to %s at %#010x -- in flash" % (ENTRY, m.group(3), tgt))
    if "<%s>:" % ENTRY not in dis:
        bad.append("could not disassemble %s" % ENTRY)
    for p in bad:
        print("checksram: %s" % p, file=sys.stderr)
    if bad:
        return 1
    print("checksram: core1 is SRAM-resident (%s@%#x, %d branches checked)"
          % (ENTRY, syms[ENTRY], calls))
    return 0


if __name__ == "__main__":
    sys.exit(main())
