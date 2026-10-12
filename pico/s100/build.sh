#!/usr/bin/env bash
# build.sh -- the S-100 clients (CP/M .COM files, 8080 only) into build/client.
# Needs z88dk; z88dk-z80asm -m8080 refuses any Z80-only instruction.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
Z88DK=${Z88DK:-$HOME/Workspace/z88dk}
ASM=$Z88DK/bin/z88dk-z80asm
OUT=$HERE/build/client
mkdir -p "$OUT"
# FNBIOS carries its resident part, assembled on its own for E800h.
"$ASM" -m8080 -b -I"$HERE/client" -O"$OUT" -o"$OUT/fnbres.bin" "$HERE/client/fnbres.asm"
rm -f "$OUT/fnbres.o"
for prog in fnhello fntest mdmtest prntest fnbios bootcheck loader; do
    "$ASM" -m8080 -b -I"$HERE/client" -I"$OUT" -O"$OUT" -o"$OUT/$prog.bin" "$HERE/client/$prog.asm"
    mv "$OUT/$prog.bin" "$OUT/${prog^^}.COM"
    rm -f "$OUT/$prog".o
    echo "${prog^^}.COM: $(stat -c %s "$OUT/${prog^^}.COM") bytes"
done
mv "$OUT/BOOTCHECK.COM" "$OUT/bootcheck.bin"
# A one-record text file for the FNBIOS scenario: CR LF, ^Z, padding.
python3 -I -c 'import sys; t = b"FUJINET S-100 FNBIOS LST TEST\r\n\x1a"; open(sys.argv[1], "wb").write(t + b"\x1a" * (128 - len(t)))' "$OUT/HELLO.TXT"
mv "$OUT/LOADER.COM" "$OUT/loader.bin"

# The boot page and the card's images, for build-cart.sh and the SIMH graft.
# CONFIG is $FUJI_CONFIG (the fujinet-config build), else the BOOTCHECK stand-in.
CONFIG=${FUJI_CONFIG:-$OUT/bootcheck.bin}
python3 -I "$HERE/tools/mkpage.py" "$HERE/build/gen" "$HERE/client/bootpage.asm" "$CONFIG" "$OUT/loader.bin"
