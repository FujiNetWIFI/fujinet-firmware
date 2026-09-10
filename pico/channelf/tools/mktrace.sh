#!/usr/bin/env bash
# Capture golden ROMC traces by running the console clients under a MAME whose
# F8 core has been instrumented by emu/f8trace.py.
#
# The traces are what test_romc.c replays. Without them that test skips, and
# nothing at all covers the cart's ROMC state machine -- MAME's cart device
# only ever sees flat reads and writes.
set -euo pipefail

cd "$(dirname "$0")/.."
MAME="${1:-$HOME/Workspace/mame}"
FRAMES="${FRAMES:-90}"
OUT="$PWD/build/trace"

[ -x "$MAME/mame" ] || { echo "mktrace: no mame binary in $MAME" >&2; exit 1; }

python3 emu/f8trace.py "$MAME"
if ! grep -q FujiNet "$MAME/src/devices/cpu/f8/f8.cpp"; then
    echo "mktrace: f8.cpp is not instrumented" >&2; exit 1
fi
echo "mktrace: rebuild MAME if f8.cpp changed:  make -C $MAME -j\$(nproc) NOWERROR=1"

mkdir -p "$OUT"
for c in hello romctest; do
    [ -f "build/$c.bin" ] || ./build.sh "$c"
    ( cd "$MAME" && F8_ROMC_TRACE="$OUT/$c.trace" SHOT_FRAMES="$FRAMES" \
        ./mame channelf -bios sl31253 -cart "$OLDPWD/build/$c.bin" \
          -autoboot_script "$OLDPWD/emu/shot.lua" \
          -snapshot_directory "$OLDPWD/build/snap" \
          -video none -sound none -nothrottle -seconds_to_run 30 >/dev/null 2>&1 )
    # Stamp the image the trace was captured from. A trace is only meaningful
    # against the exact bytes that produced it -- edit a shared include and
    # every client shifts, and the replay then fails deep in the run with a
    # register divergence that looks like an observer bug.
    sha256sum "build/$c.bin" | cut -d" " -f1 > "$OUT/$c.sha"
    n=$(( $(stat -c%s "$OUT/$c.trace") / 10 ))
    echo "mktrace: $c.trace: $n bus cycles"
done
