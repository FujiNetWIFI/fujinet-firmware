#!/usr/bin/env bash
# build.sh -- the Model I clients into build/client: each as a bootable 35-track JV1
# test disk (NAME.dsk) and as a /CMD for running under a DOS. With $FUJI_CONFIG
# (fujinet-config's trs80m1 config.cmd) also autorun.jv1, the CONFIG disk for slot 1.
# Needs z88dk.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
Z88DK=${Z88DK:-$HOME/Workspace/z88dk}
ASM=$Z88DK/bin/z88dk-z80asm
OUT=$HERE/build/client
INC=(-I"$HERE/client" -I"$HERE/../common/client")
mkdir -p "$OUT"

"$ASM" -b "${INC[@]}" -O"$OUT" -o"$OUT/boot.bin" "$HERE/client/boot.asm"
for prog in fnhello fntest ramtest fdctest prntest mdmtest; do
    "$ASM" -b "${INC[@]}" -O"$OUT" -o"$OUT/$prog.bin" "$HERE/client/$prog.asm"
    python3 -I "$HERE/tools/mkjv1.py" boot "$OUT/boot.bin" "$OUT/$prog.bin" "$OUT/$prog.dsk" --tracks 35
    python3 -I "$HERE/tools/mkjv1.py" cmd "$OUT/$prog.bin" "$OUT/${prog^^}.CMD"
done
rm -f "$OUT"/*.o
python3 -I "$HERE/tools/mkjv1.py" pattern "$OUT/pattern.dsk" --tracks 35
if [ -n "${FUJI_CONFIG:-}" ]; then
    python3 -I "$HERE/tools/mkjv1.py" cmdboot "$OUT/boot.bin" "$FUJI_CONFIG" "$OUT/autorun.jv1" \
        --needs-ram
fi
