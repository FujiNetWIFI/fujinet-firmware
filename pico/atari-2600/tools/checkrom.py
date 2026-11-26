#!/usr/bin/env python3
"""checkrom.py -- check a built 2600 client against firmware/include/fuji_mailbox.h.

Bans read-modify-write on the mailbox: with no R/W line the cart keeps the
second-to-last data sample, and an RMW's burst of writes has no defined one.
The 6507 has no interrupts, so a static scan of the code is a proof.
--data [B:]$LO-$HI skips a jump table (bank B only, or every bank), whose
address bytes would otherwise decode as STA (zp,X).

Usage: checkrom.py [--data [B:]$LO-$HI]... image.bin [image2.bin ...]
"""

import sys

WINDOW = 0x1000              # FN_WINDOW_SIZE
BANK = 0x800                 # FN_BANK_SIZE
BASE = 0x1000                # FN_WINDOW_BASE
CLAIM = 0x1F10               # FN_R_CLAIM
CLAIM_SIG = b"FUJI"

# The control and TX pages: write-only, and never to be read or RMW'd.
WR_LO, WR_HI = 0x1D00, 0x1EFF

# MAME's vcs_cart_slot_device::call_load() rejects anything else outright.
MAME_SIZES = (0x800, 0x1000, 0x2000, 0x28ff, 0x2900, 0x3000,
              0x4000, 0x8000, 0x10000, 0x80000)

# Read-modify-write opcodes with an absolute or absolute,X operand.
RMW = {
    0x0E: "ASL abs", 0x1E: "ASL abs,X",
    0x2E: "ROL abs", 0x3E: "ROL abs,X",
    0x4E: "LSR abs", 0x5E: "LSR abs,X",
    0x6E: "ROR abs", 0x7E: "ROR abs,X",
    0xCE: "DEC abs", 0xDE: "DEC abs,X",
    0xEE: "INC abs", 0xFE: "INC abs,X",
}
# Indirect JMP, for the page-wrap bug.
JMP_IND = 0x6C

# STA (zp),Y reads the address it is about to write; on a write-only page that
# read is a store of the floating bus. The pointer is unknowable, so the mode is
# banned everywhere: jump to a stub that stores absolutely instead.
STA_IND = {0x91: "STA (zp),Y", 0x81: "STA (zp,X)"}

# Opcode lengths for a linear scan. Undocumented opcodes count as 1 byte and
# can desynchronize it: a guard, not a disassembler.
LEN = [1] * 256
for op in (0x69, 0x29, 0xC9, 0xE0, 0xC0, 0x49, 0xA9, 0xA2, 0xA0, 0x09, 0xE9,
           0xA5, 0xA6, 0xA4, 0x85, 0x86, 0x84, 0x65, 0x25, 0x06, 0x24, 0xC5,
           0xC6, 0x45, 0xE6, 0x46, 0x26, 0x66, 0xE5, 0x05, 0x75, 0x35, 0x16,
           0xD5, 0xD6, 0x55, 0xF6, 0x56, 0x36, 0x76, 0xF5, 0x15, 0xB5, 0xB4,
           0x95, 0x94, 0xB6, 0x96, 0x61, 0x21, 0xC1, 0x41, 0xA1, 0x01, 0xE1,
           0x81, 0x71, 0x31, 0xD1, 0x51, 0xB1, 0x11, 0xF1, 0x91,
           0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0):
    LEN[op] = 2
for op in (0x6D, 0x2D, 0x0E, 0x2C, 0xCD, 0xEC, 0xCC, 0xCE, 0x4D, 0xEE, 0x4C,
           0x20, 0xAD, 0xAE, 0xAC, 0x4E, 0x0D, 0x2E, 0x6E, 0xED, 0x8D, 0x8E,
           0x8C, 0x7D, 0x3D, 0x1E, 0xDD, 0xDE, 0x5D, 0xFD, 0xFE, 0x5E, 0xBD,
           0xBC, 0x3E, 0x7E, 0x1D, 0x9D, 0x79, 0x39, 0xD9, 0x59, 0xB9, 0xBE,
           0x19, 0xF9, 0x99, 0x6C):
    LEN[op] = 3


def check(path, data=()):
    img = open(path, "rb").read()
    bad = []

    if len(img) < 2 * BANK or len(img) % BANK:
        return ["size is %d, must be (N+1) * %d" % (len(img), BANK)]
    if len(img) not in MAME_SIZES:
        bad.append("size %d is not on MAME's cartridge whitelist %s -- "
                   "vcs_cart_slot_device::call_load() would reject it"
                   % (len(img), [hex(s) for s in MAME_SIZES if s >= 0x800]))

    # The fixed half is the LAST 2K and carries the claim and the vectors.
    fixed = len(img) - BANK
    off = fixed + (CLAIM - 0x1800)
    if img[off:off + 4] != CLAIM_SIG:
        bad.append("claim %r missing at image offset %#x" % (CLAIM_SIG, off))

    # A console RESET keeps the current bank, so a banked client's reset vector
    # must point into the fixed half.
    vec = img[fixed + 0x7FC] | (img[fixed + 0x7FD] << 8)
    banked = fixed > BANK
    lo = 0x1800 if banked else BASE
    if not (lo <= vec <= 0x1FFF):
        bad.append("reset vector $%04X is not in the %s"
                   % (vec, "fixed half" if banked else "window"))

    # A "FUJI" anywhere but the claim offset is inert (on-screen text has one).

    # Every bank is code, entered at $1000 by the fixed tail's trampoline.
    banks = [(b * BANK, "bank %d" % b) for b in range(fixed // BANK)]
    for start, label in banks + [(fixed, "fixed half")]:
        pc = 0
        # Every bank is mapped at $1000, the fixed half at $1800.
        base = 0x1800 if start == fixed else BASE
        bank = start // BANK if start != fixed else None
        while pc < BANK - 2:
            addr = base + pc
            skip = next((hi - addr + 1 for b, lo, hi in data
                         if (b is None or b == bank) and lo <= addr <= hi),
                        0)
            if skip:
                pc += skip
                continue
            op = img[start + pc]
            n = LEN[op]
            if n == 3:
                tgt = img[start + pc + 1] | (img[start + pc + 2] << 8)
                if op in RMW and WR_LO <= tgt <= WR_HI:
                    bad.append(
                        "%s $%04X: %s targets the write-only page at $%04X -- "
                        "an RMW is three bus cycles at one address and the "
                        "cartridge cannot recover a defined byte from it"
                        % (label, base + pc, RMW[op], tgt))
                if op == JMP_IND and (tgt & 0xFF) == 0xFF:
                    bad.append("%s $%04X: JMP ($%04X) hits the 6502 "
                               "page-wrap bug" % (label, base + pc, tgt))
            if op in STA_IND:
                bad.append(
                    "%s $%04X: %s -- an indirect store performs a read at the "
                    "address it writes, and on the control or TX page that "
                    "read is decoded as a stray access. Dispatch through an "
                    "indirect JMP to an absolute store instead"
                    % (label, base + pc, STA_IND[op]))
            pc += n
    return bad


def parse_data(spec):
    """$1204-$1219, 1204-1219, or 3:$1204-$1219 (bank 3 only). Console
    addresses, inclusive."""
    bank = None
    if ":" in spec:
        b, _, spec = spec.partition(":")
        bank = int(b, 0)
    lo, _, hi = spec.partition("-")
    if not hi:
        raise SystemExit("checkrom: --data wants [B:]LO-HI, got %r" % spec)
    return (bank, int(lo.lstrip("$"), 16), int(hi.lstrip("$"), 16))


def main():
    rc = 0
    data, paths, args = [], [], list(sys.argv[1:])
    while args:
        a = args.pop(0)
        if a == "--data":
            if not args:
                raise SystemExit("checkrom: --data needs a range")
            data.append(parse_data(args.pop(0)))
        elif a.startswith("--data="):
            data.append(parse_data(a.split("=", 1)[1]))
        else:
            paths.append(a)
    for path in paths:
        problems = check(path, data)
        for p in problems:
            print("checkrom: %s: %s" % (path, p), file=sys.stderr)
        if problems:
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
