#!/usr/bin/env bash
# run.sh -- run a built client in MAME.
#
#   ./run.sh <client> [lua-script]
#
# With a script it runs headless and exits; without one it opens a window.
#
# Two environment facts this wraps, both of which cost time to rediscover:
#   - MAME must run FROM ITS OWN TREE or -autoboot_script is silently ignored.
#   - SDL_VIDEODRIVER=dummy is required wherever there is no DISPLAY; without
#     it MAME dies with "Could not initialize SDL No available video device"
#     even under -video none, because SDL is brought up before the video
#     backend is chosen.
#
# fujinet-pc's BoIP listener takes ONE client (backlog 1), so a MAME left
# running silently starves the next run and the symptom is a hang, not an
# error. Kill any stray first.

set -euo pipefail
cd "$(dirname "$0")"
HERE=$(pwd)

CLIENT=${1:-hello}
SCRIPT=${2:-}
MAME=${MAME:-$HOME/Workspace/mame}
SLOT=${SLOT:-a26_2k_4k}          # M1 onward: SLOT=fujinet
SNAP=${SNAP:-$HERE/build/snap}

pkill -f "mame a2600" 2>/dev/null || true
mkdir -p "$SNAP"

args=(a2600 -cartslot "$SLOT" -cart "$HERE/build/$CLIENT.bin"
      -snapshot_directory "$SNAP")

# Lua harnesses write their artefacts next to the build, not into the MAME
# tree we have to cd into.
export DRIVE_EXPECT="${DRIVE_EXPECT:-$HERE/build/expect.txt}"
# Lua harnesses require() shared modules out of emu/.
export A2600_EMU="$HERE/emu"

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle
           -seconds_to_run "${SECS:-10}")
fi

[ -n "${DISPLAY:-}" ] || export SDL_VIDEODRIVER=dummy

cd "$MAME"
exec ./mame "${args[@]}"
