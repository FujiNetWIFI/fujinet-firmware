#!/usr/bin/env python3
"""
Build a bootable HD20 volume image for the FujiNet Mac 68k from another
HFS volume image (for example a 2 GB or 40 MB SavageTaylor volume that
the HD20 driver cannot use): copies every file and folder, keeps the
source's boot blocks, blesses the System Folder, and writes a volume of
at most 65,535 blocks (32 MB), the largest an HD20 accepts.

    python3 -m venv venv && ./venv/bin/pip install machfs
    ./venv/bin/python make_hd20_volume.py SRC.dsk OUT.dsk [--name NAME] [--blocks 65535]
"""
import argparse, struct, sys
import machfs

ap = argparse.ArgumentParser()
ap.add_argument("src"); ap.add_argument("out")
ap.add_argument("--name", default="System")
ap.add_argument("--blocks", type=int, default=65535)
a = ap.parse_args()

src_bytes = open(a.src, "rb").read()
src = machfs.Volume(); src.read(src_bytes)
dst = machfs.Volume(); dst.name = a.name
for k in src.keys():
    dst[k] = src[k]
out = bytearray(dst.write(size=a.blocks * 512, align=512, desktopdb=True, bootable=True))

# boot blocks: machfs leaves them empty, take the source's
out[:1024] = src_bytes[:1024]

# bless the system folder (whatever it is called): the root folder that
# holds a file "System" of type ZSYS. MDB drFndrInfo[0] = its directory ID
sysfolder = None
for k, v in dst.items():
    if isinstance(v, machfs.Folder) and isinstance(v.get("System"), machfs.File) and v["System"].type == b"ZSYS":
        sysfolder = k
if sysfolder is None:
    sys.exit("no folder with a System file at the root of the volume")
name = sysfolder.encode("mac_roman")
pat = b"\x00\x00\x00\x02" + bytes([len(name)]) + name   # catalog key: parent 2 (root), name
i = out.find(pat)
if i < 0:
    sys.exit(f"catalog key for '{sysfolder}' not found")
print("system folder:", sysfolder)
key = i - 2; rec = key + 1 + out[key]; rec += rec & 1
if out[rec] != 1:
    sys.exit("catalog record for System Folder is not a directory record")
cnid = struct.unpack(">I", out[rec + 6:rec + 10])[0]
struct.pack_into(">I", out, 1024 + 0x5C, cnid)
struct.pack_into(">I", out, 1024 + 0x5C + 8, 2)
open(a.out, "wb").write(out)
print(f"{a.out}: {len(out)} bytes, System Folder id {cnid} blessed, boot blocks {out[:2]!r}")
