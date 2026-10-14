#!/usr/bin/env bash
# fetch-disks.sh -- the deramp.com Altair images run.sh needs beyond z80pack's
# 8" library: minidisk CP/M and BASIC, and the FDC+ 8 MB CP/M. They land in the
# git-ignored build/disks and are never committed.
set -euo pipefail
OUT=$(cd "$(dirname "$0")/.." && pwd)/build/disks
BASE=https://deramp.com/downloads/altair/software
mkdir -p "$OUT"
get() {
    [ -s "$OUT/$2" ] || curl -sSfL -o "$OUT/$2" "$BASE/$1"
    echo "$2: $(stat -c %s "$OUT/$2") bytes"
}
get "minidisk/CPM%202.2/CPM56K-1.DSK" mini-cpm22-56k.dsk
get "minidisk/BASIC/(Boot)Basic%20300-5-F.dsk" mini-basic50.dsk
get "8_inch_floppy/CPM/CPM%202.2/FDC+%208Mb%20CPM%202.2/CPM22-8MB-56K.DSK" fdcp-8mb-cpm22-56k.dsk
# A blank 8 MB image of 512-byte sectors for FNBIOS's drives, E5h throughout
# so CP/M sees an empty directory.
[ -s "$OUT/blank8m.img" ] || python3 -I -c 'import sys; open(sys.argv[1], "wb").write(b"\xe5" * 8388608)' "$OUT/blank8m.img"
echo "blank8m.img: $(stat -c %s "$OUT/blank8m.img") bytes"

# The MITS minidisk boot loader; DBL itself comes from z80pack.
B2=https://deramp.com/downloads/altair/software/roms/orginal_roms
[ -s "$OUT/mdbl.hex" ] || curl -sSfL -o "$OUT/mdbl.hex" "$B2/MDBL.HEX"
echo "mdbl.hex: $(stat -c %s "$OUT/mdbl.hex") bytes"
