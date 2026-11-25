#!/usr/bin/env python3
"""mkjv1.py -- disks and load files for the Model I card's clients.

    mkjv1.py boot BOOT.BIN PROGRAM.BIN OUT [--load ADDR] [--entry ADDR] [--needs-ram]
             [--tracks N]
        A JV1 image whose track 0 sector 0 is BOOT.BIN (boot.asm) with its "FNM1"
        block filled in, and PROGRAM.BIN from track 1 on. Without --tracks it is
        as short as the program allows; a .dsk wants 35, 40 or 80.
    mkjv1.py cmd PROGRAM.BIN OUT [--load ADDR] [--entry ADDR]
        A TRS-80 /CMD load module, for running the program under a DOS.
    mkjv1.py cmdboot BOOT.BIN PROGRAM.CMD OUT [--needs-ram] [--tracks N]
        As boot, from a /CMD: its blocks laid out from the lowest address, and its
        transfer address the entry.
    mkjv1.py pattern OUT [--tracks N]
        A JV1 image whose byte n of sector s (counted from the start) is s * 7 + n.
"""
import argparse
import sys

SECTOR = 256
SPT = 10
TRACK = SECTOR * SPT


def number(text):
    return int(text, 0)


def boot(args):
    boot_sector = bytearray(open(args.boot, "rb").read())
    program = open(args.program, "rb").read()
    if len(boot_sector) != SECTOR:
        sys.exit("mkjv1: the boot sector is %d bytes, not 256" % len(boot_sector))
    info = boot_sector.find(b"FNM1")
    if info < 0:
        sys.exit("mkjv1: the boot sector has no FNM1 block")
    sectors = (len(program) + SECTOR - 1) // SECTOR
    if args.load + sectors * SECTOR > 0x10000:
        sys.exit("mkjv1: the program doesn't fit above %04Xh" % args.load)
    pos = info + 4
    boot_sector[pos:pos + 2] = args.load.to_bytes(2, "little")
    boot_sector[pos + 2:pos + 4] = (args.entry if args.entry is not None else args.load).to_bytes(2, "little")
    boot_sector[pos + 4:pos + 6] = sectors.to_bytes(2, "little")
    boot_sector[pos + 6] = 1 if args.needs_ram else 0

    image = bytearray(boot_sector) + bytes(TRACK - SECTOR)
    image += program + bytes(sectors * SECTOR - len(program))
    tracks = (len(image) + TRACK - 1) // TRACK
    if args.tracks:
        if args.tracks < tracks:
            sys.exit("mkjv1: the program needs %d tracks" % tracks)
        tracks = args.tracks
    image += b"\xE5" * (tracks * TRACK - len(image))
    open(args.out, "wb").write(image)
    print("mkjv1: %s: %d tracks, %d program sectors at %04Xh" % (args.out, tracks, sectors, args.load))


def cmd(args):
    program = open(args.program, "rb").read()
    out = bytearray()
    for pos in range(0, len(program), 254):
        chunk = program[pos:pos + 254]
        # A record's length byte counts its address too; 0 means 256.
        out += bytes([0x01, (len(chunk) + 2) & 0xFF]) + (args.load + pos).to_bytes(2, "little")
        out += chunk
    out += bytes([0x02, 0x02]) + (args.entry if args.entry is not None else args.load).to_bytes(2, "little")
    open(args.out, "wb").write(out)
    print("mkjv1: %s: %d bytes at %04Xh" % (args.out, len(program), args.load))


def read_cmd(path):
    """The memory image a /CMD loads and its transfer address."""
    data = open(path, "rb").read()
    blocks, entry, pos = [], None, 0
    while pos + 2 <= len(data):
        kind, size = data[pos], data[pos + 1]
        pos += 2
        if kind == 0x01:
            size = size if size > 2 else size + 256  # 0, 1, 2 stand for 256-258
            addr = int.from_bytes(data[pos:pos + 2], "little")
            blocks.append((addr, data[pos + 2:pos + size]))
        elif kind == 0x02:
            entry = int.from_bytes(data[pos:pos + 2], "little")
            break
        pos += size
    if not blocks or entry is None:
        sys.exit("mkjv1: %s is not a /CMD with a transfer address" % path)
    low = min(addr for addr, _ in blocks)
    high = max(addr + len(body) for addr, body in blocks)
    image = bytearray(high - low)
    for addr, body in blocks:
        image[addr - low:addr - low + len(body)] = body
    return low, entry, bytes(image)


def cmdboot(args):
    args.load, args.entry, program = read_cmd(args.cmd)
    args.program = args.cmd + ".bin"
    open(args.program, "wb").write(program)
    boot(args)


def pattern(args):
    image = bytearray(args.tracks * TRACK)
    for pos in range(len(image)):
        image[pos] = (pos // SECTOR * 7 + pos % SECTOR) & 0xFF
    open(args.out, "wb").write(image)
    print("mkjv1: %s: %d patterned tracks" % (args.out, args.tracks))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="what", required=True)
    one = sub.add_parser("boot")
    one.add_argument("boot")
    one.add_argument("program")
    one.add_argument("out")
    one.add_argument("--load", type=number, default=0x5200)
    one.add_argument("--entry", type=number)
    one.add_argument("--needs-ram", action="store_true")
    one.add_argument("--tracks", type=int)
    one.set_defaults(run=boot)
    two = sub.add_parser("cmd")
    two.add_argument("program")
    two.add_argument("out")
    two.add_argument("--load", type=number, default=0x5200)
    two.add_argument("--entry", type=number)
    two.set_defaults(run=cmd)
    four = sub.add_parser("cmdboot")
    four.add_argument("boot")
    four.add_argument("cmd")
    four.add_argument("out")
    four.add_argument("--needs-ram", action="store_true")
    four.add_argument("--tracks", type=int)
    four.set_defaults(run=cmdboot)
    three = sub.add_parser("pattern")
    three.add_argument("out")
    three.add_argument("--tracks", type=int, default=35)
    three.set_defaults(run=pattern)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
