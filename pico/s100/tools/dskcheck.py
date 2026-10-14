#!/usr/bin/env python3
"""dskcheck.py -- check fujinet-pc's MediaTypeAltair end to end over BoIP.

For each image (a path under the SD host, plus the same file on disk): mount
it read-only into slot 0, read the PERCOM block, read every sector and compare
it with the file. With --write, mount it writable, rewrite every sector with a
pattern, read it back, restore it, and check the file size never changed.

    FUJINET_TCP=127.0.0.1:1995 tools/dskcheck.py SD_DIR /altair/x.dsk [...] [--write]
"""
import os
import struct
import sys

sys.dont_write_bytecode = True                  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(__file__))
import fujibus  # noqa: E402

SECTOR = 137
SLOT = 0


def percom(bus):
    ok, data = bus.call(fujibus.DISK + SLOT, ord("N"))
    if not ok or len(data) != 12:
        raise RuntimeError("PERCOM read failed")
    tracks, _, spt, _, _, size, present = struct.unpack(">BBHBBHB", data[:9])
    return tracks or 2048, spt, size, present


def read(bus, lba):
    ok, data = bus.call(fujibus.DISK + SLOT, ord("R"), [(4, lba)])
    if not ok:
        raise RuntimeError("read of sector %d refused" % lba)
    return data


def write(bus, lba, data):
    ok, _ = bus.call(fujibus.DISK + SLOT, ord("W"), [(4, lba)], data)
    if not ok:
        raise RuntimeError("write of sector %d refused" % lba)


def check(bus, sd, path, do_write):
    local = os.path.join(sd, path.lstrip("/"))
    image = open(local, "rb").read()
    size = len(image)

    bus.mount(SLOT, 0, path, 1)
    tracks, spt, ssize, present = percom(bus)
    if ssize != SECTOR or present != 0xFF:
        raise RuntimeError("%s: PERCOM says %d-byte sectors" % (path, ssize))
    n = tracks * spt
    for lba in range(n):
        if read(bus, lba) != image[lba * SECTOR:(lba + 1) * SECTOR]:
            raise RuntimeError("%s: sector %d differs" % (path, lba))
    print("%s: %d x %d x %d, %d sectors byte-exact" % (path, tracks, spt, ssize, n))

    if do_write:
        bus.mount(SLOT, 0, path, 2)
        for lba in range(n):
            write(bus, lba, bytes((lba + i) & 0xFF for i in range(SECTOR)))
        for lba in range(n):
            if read(bus, lba) != bytes((lba + i) & 0xFF for i in range(SECTOR)):
                raise RuntimeError("%s: sector %d did not read back" % (path, lba))
        for lba in range(n):
            write(bus, lba, image[lba * SECTOR:(lba + 1) * SECTOR])
        bus.unmount(SLOT)
        if os.path.getsize(local) != size or open(local, "rb").read() != image:
            raise RuntimeError("%s: file changed size or content after restore" % path)
        print("%s: write, read back and restore OK, still %d bytes" % (path, size))
    bus.unmount(SLOT)


def main():
    args = [a for a in sys.argv[1:] if a != "--write"]
    if len(args) < 2:
        sys.exit(__doc__)
    bus = fujibus.Bus()
    for path in args[1:]:
        check(bus, args[0], path, "--write" in sys.argv)
    print("DSKCHECK PASS")


if __name__ == "__main__":
    main()
