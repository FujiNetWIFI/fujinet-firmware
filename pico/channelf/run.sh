#!/usr/bin/env bash
# run.sh -- run a Channel F client in MAME against a live fujinet-pc.
#
#   ./run.sh fujitest                 interactive
#   ./run.sh fujiboot boottest        headless, driven by emu/boottest.lua
#   MAME=~/src/mame ./run.sh hello    a different MAME tree
#
# MAME must run from its own tree or -autoboot_script is silently ignored, so
# everything below is passed as an absolute path.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$PWD"

MAME="${MAME:-$HOME/Workspace/mame}"
CLIENT="${1:-fujitest}"
SCRIPT="${2:-}"

[ -x "$MAME/mame" ] || { echo "run.sh: no mame binary in $MAME" >&2; exit 1; }
[ -f "build/$CLIENT.bin" ] || ./build.sh "$CLIENT"

args=(channelf -bios sl31253 -cartslot fujinet -cart "$HERE/build/$CLIENT.bin"
      -snapshot_directory "$HERE/build/snap")

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle -seconds_to_run "${SECS:-60}")
else
    args+=(-window)
fi

cd "$MAME"
exec ./mame "${args[@]}"
