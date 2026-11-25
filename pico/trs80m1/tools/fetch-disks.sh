#!/usr/bin/env bash
# fetch-disks.sh -- the freely downloadable Model I DOS images the scenarios boot, into
# build/disks (git-ignored; never committed). Other DOSes (DOSPLUS, MultiDOS, a full
# TRSDOS 2.3) go into build/disks by hand, as JV1.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=$HERE/build/disks
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

# LDOS 5.3.1: MISOSYS's free distribution, on Tim Mann's site; MAME's ld1-531.jv1.
curl -fsSL -o "$TMP/ld1-531.zip" https://www.tim-mann.org/trs80/ld1-531.zip
unzip -o -q -d "$TMP" "$TMP/ld1-531.zip" ld1-531.dsk
cp "$TMP/ld1-531.dsk" "$OUT/ldos531.dsk"
echo "373d0887bc4946df010f39842e768bf911cb2813  $OUT/ldos531.dsk" | sha1sum -c --quiet

# NEWDOS/80 2.0, 80 tracks, from the System 80 software archive.
curl -fsSL -o "$OUT/newdos80.dsk" \
    https://www.classic-computers.org.nz/system-80/disks/NEWDOS_80sssd_jv1.DSK
echo "68a6708076a2c24ddd6e6fe7279add086502e4bc  $OUT/newdos80.dsk" | sha1sum -c --quiet

# TRSDOS 2.3, cut down to boot the game Xenos (no SYS6: no DIR or FREE), same archive.
curl -fsSL -o "$OUT/trsdos23.dsk" \
    https://www.classic-computers.org.nz/system-80/disks/XENOS_trsdos-drive0.DSK
echo "a438b509e753fed99e98f5f3509e68e76f92f46e  $OUT/trsdos23.dsk" | sha1sum -c --quiet
echo "fetch-disks.sh: $(ls "$OUT" | tr '\n' ' ')"
