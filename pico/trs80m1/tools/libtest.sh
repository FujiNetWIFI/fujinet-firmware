#!/usr/bin/env bash
# libtest.sh -- fujinet-lib's test programs for the trs80m1 platform, each made a boot
# disk in build/libtest for `run.sh libtest --set PROG=NAME`. Builds them first in
# $FNLIB (a fujinet-lib checkout) with z88dk.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
FNLIB=${FNLIB:-$HOME/Workspace/fujinet-lib}
Z88DK=${Z88DK:-$HOME/Workspace/z88dk}
export PATH=$Z88DK/bin:$PATH ZCCCFG=$Z88DK/lib/config
make -C "$FNLIB" trs80m1
make -C "$FNLIB/testing" trs80m1 GIT_VERSION="$(git -C "$FNLIB" rev-parse --short HEAD)"
OUT=$HERE/build/libtest
mkdir -p "$OUT"
for cmd in "$FNLIB"/testing/r2r/trs80m1/*.cmd; do
    name=$(basename "$cmd" .cmd)
    cp "$cmd" "$OUT/$name.cmd"
    python3 -I "$HERE/tools/mkjv1.py" cmdboot "$HERE/build/client/boot.bin" "$OUT/$name.cmd" \
        "$OUT/$name.dsk" --tracks 35 --needs-ram
    rm -f "$OUT/$name.cmd.bin"
done
