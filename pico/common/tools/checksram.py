#!/usr/bin/env python3
"""checksram.py -- core1's bus loop, everything it branches to, and the state it shares must
be in SRAM: core1 may never take an XIP miss mid-cycle.

    checksram.py firmware.elf core1_entry [symbol ...] [--no-calls]

Fails if a named symbol is outside SRAM, or if the entry branches out of SRAM, through a
linker veneer, or through a register (core1 never calls through a pointer). --no-calls also
fails on any call to another function, for a loop that must have everything inline.
fujicart_checksram() runs it after every link.
"""

import re
import subprocess
import sys

SRAM_LO = 0x20000000
SRAM_HI = {"v6S-M": 0x20042000, "v8-M.mainline": 0x20082000}   # RP2040, RP2350


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def main():
    args = [a for a in sys.argv[1:] if a != "--no-calls"]
    no_calls = len(args) != len(sys.argv) - 1
    if len(args) < 2:
        sys.exit(__doc__.split("\n\n")[1])
    elf, entry, data = args[0], args[1], args[2:]

    arch = re.search(r"Tag_CPU_arch: (\S+)", run("arm-none-eabi-readelf", "-A", elf))
    if not arch or arch.group(1) not in SRAM_HI:
        sys.exit("checksram: %s: not an RP2040 or RP2350 image" % elf)
    hi = SRAM_HI[arch.group(1)]

    syms = {}
    for line in run("arm-none-eabi-nm", elf).splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    bad = []
    for name in [entry] + data:
        if name not in syms:
            bad.append("%s is not in the image" % name)
        elif not SRAM_LO <= syms[name] < hi:
            bad.append("%s is at %#010x -- not in SRAM" % (name, syms[name]))

    dis = run("arm-none-eabi-objdump", "-d", "--no-show-raw-insn", "--disassemble=" + entry,
              elf)
    if "<%s>:" % entry not in dis:
        bad.append("could not disassemble %s" % entry)
    branches = 0
    for line in dis.splitlines():
        if re.search(r"\s(blx|bx)\s+(r\d+|ip|sb|sl|fp)\b", line):
            bad.append("%s branches through a register: %s" % (entry, line.strip()))
            continue
        m = re.search(r"\s(bl|blx|b\.?\w*|cb\w+)\s+([0-9a-f]+)\s*<([^>]+)>", line)
        if not m:
            continue
        branches += 1
        tgt, name = int(m.group(2), 16), m.group(3)
        if not SRAM_LO <= tgt < hi:
            bad.append("%s branches to %s at %#010x -- in flash" % (entry, name, tgt))
        elif "_veneer" in name:
            bad.append("%s calls %s -- a veneer out of SRAM" % (entry, name))
        elif no_calls and m.group(1) in ("bl", "blx") and not name.startswith(entry):
            bad.append("%s calls %s -- every call on the bus path should be inline"
                       % (entry, name))
    for p in bad:
        print("checksram: %s" % p, file=sys.stderr)
    if bad:
        return 1
    print("checksram: core1 is SRAM-resident (%s@%#x, %d branches%s)"
          % (entry, syms[entry], branches, ", no calls out" if no_calls else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
