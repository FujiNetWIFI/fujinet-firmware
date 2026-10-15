#!/usr/bin/env python3
"""checksram.py -- core1's bus loop, and everything it calls, must be in SRAM.

The F8 has no address bus: a cart that misses one bus cycle tracks PC0/DC0
wrong for the rest of the session, so core1 may never take an XIP miss. The
map grep in CI only proves channelf_core1_main itself is in SRAM. This also
disassembles it and fails on any branch that leaves SRAM -- including through
a linker veneer, which sits in SRAM itself and only then jumps out to flash
(how an un-inlined chf_read() hid there until the decode was forced inline).

Usage: checksram.py fujichannelf.elf
"""

import re
import subprocess
import sys

SRAM_LO, SRAM_HI = 0x20000000, 0x20042000
ENTRY = "channelf_core1_main"
DATA = ("fuji_win", "fuji_arena", "fuji_ring",
        "fuji_live_win", "fuji_mailbox_active")


def disasm(elf, sym):
    return subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn",
                           "--disassemble=" + sym, elf],
                          capture_output=True, text=True, check=True).stdout


def main():
    elf = sys.argv[1]
    nm = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True,
                        text=True, check=True).stdout
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

    dis = disasm(elf, ENTRY)
    if "<%s>:" % ENTRY not in dis:
        bad.append("could not disassemble %s" % ENTRY)
    branches = 0
    for line in dis.splitlines():
        m = re.search(r"\s(bl|blx|b\.?\w*|cb\w+)\s+([0-9a-f]+)\s*<([^>]+)>", line)
        if not m:
            continue
        branches += 1
        tgt, name = int(m.group(2), 16), m.group(3)
        if not SRAM_LO <= tgt < SRAM_HI:
            bad.append("%s branches to %s at %#010x -- in flash" % (ENTRY, name, tgt))
        elif "_veneer" in name:
            bad.append("%s calls %s -- a veneer out of SRAM" % (ENTRY, name))
        elif m.group(1) in ("bl", "blx") and not name.startswith(ENTRY):
            bad.append("%s calls %s -- every call on the bus path should be inline"
                       % (ENTRY, name))
    for p in bad:
        print("checksram: %s" % p, file=sys.stderr)
    if bad:
        return 1
    print("checksram: core1 is SRAM-resident (%s@%#x, %d branches, no calls out)"
          % (ENTRY, syms[ENTRY], branches))
    return 0


if __name__ == "__main__":
    sys.exit(main())
