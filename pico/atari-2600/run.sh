#!/usr/bin/env bash
# run.sh -- run a built client in MAME: ./run.sh <client> [lua-script]
#
# A script runs headless and exits. MAME=<tree> MAMEBIN=./a2600 for a
# single-driver build. fujinet-pc's BoIP listener takes one client, so a MAME
# left running makes the next run hang.

set -euo pipefail
cd "$(dirname "$0")"
HERE=$(pwd)

CLIENT=${1:-hello}
SCRIPT=${2:-}
MAME=${MAME:-$HOME/Workspace/mame}
BIN=${MAMEBIN:-./mame}
SLOT=${SLOT:-a26_2k_4k}          # M1 onward: SLOT=fujinet
SNAP=${SNAP:-$HERE/build/snap}

mkdir -p "$SNAP"

args=(a2600 -cartslot "$SLOT" -cart "$HERE/build/$CLIENT.bin"
      -snapshot_directory "$SNAP")

# Harness output goes next to the build, not into the MAME tree.
export DRIVE_EXPECT="${DRIVE_EXPECT:-$HERE/build/expect.txt}"
# Lua harnesses require() shared modules out of emu/.
export A2600_EMU="$HERE/emu"

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle
           -seconds_to_run "${SECS:-10}")
fi

# SDL starts before the video backend, so -video none alone still fails with no display.
[ -n "${DISPLAY:-}" ] || export SDL_VIDEODRIVER=dummy

# Outside its own tree MAME ignores -autoboot_script without a word.
cd "$MAME"
exec "$BIN" "${args[@]}"
