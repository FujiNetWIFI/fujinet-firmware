#!/usr/bin/env python3
"""fnmount.py -- mount images into fujinet-pc's slots over BoIP, from the SD host.

    FUJINET_TCP=127.0.0.1:1995 tools/fnmount.py 0=/altair/a.dsk:rw 1=/altair/b.dsk [...]
    tools/fnmount.py --clear     unmount every slot
"""
import os
import sys

sys.dont_write_bytecode = True                  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(__file__))
import fujibus  # noqa: E402


def main():
    bus = fujibus.Bus()
    if "--clear" in sys.argv:
        for slot in range(8):
            bus.unmount(slot)
    for arg in sys.argv[1:]:
        if arg.startswith("--"):
            continue
        slot, _, spec = arg.partition("=")
        path, _, mode = spec.partition(":")
        bus.mount(int(slot), 0, path, 2 if mode == "rw" else 1)
        print("slot %s: %s %s" % (slot, path, mode or "ro"))


if __name__ == "__main__":
    main()
