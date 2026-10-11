#!/usr/bin/env bash
# run.sh -- run a Studio II client in MAME against a live fujinet-pc.
#
#   ./run.sh fujitest                 interactive
#   ./run.sh fujitest s2text          headless, driven by emu/s2text.lua
#   ./run.sh config cfgtest           the real CONFIG (fujinet-config
#                                     studio2/, make install puts it in build/)
#   IMAGE=/path/game.st2 ./run.sh -   any image, served from power-on (DIRECT)
#
# fujinet-pc must be serving BoIP on $FUJINET_TCP (default 127.0.0.1:9997,
# this bring-up's own copy; never 9995's), and only one MAME may talk to it
# at a time. MAME is the single-driver build in $MAME (default
# ~/Workspace/mame-studio2, binary ./studio2); it must run from its own tree
# or -autoboot_script is ignored.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$PWD"

MAME="${MAME:-$HOME/Workspace/mame-studio2}"
BIN="${MAMEBIN:-./studio2}"
ROMS="${ROMS:-$HOME/Workspace/mame/roms}"
CLIENT="${1:-config}"
SCRIPT="${2:-}"

[ -x "$MAME/$BIN" ] || { echo "run.sh: no $BIN in $MAME" >&2; exit 1; }

export S2_EMU_DIR="$HERE/emu"
export FUJINET=1
export FUJINET_TCP="${FUJINET_TCP:-127.0.0.1:9997}"
[ -n "${IMAGE:-}" ] && export FUJINET_IMAGE="$IMAGE"
if [ "$CLIENT" != - ]; then
    if [ ! -f "build/$CLIENT.st2" ]; then
        [ "$CLIENT" != config ] || { echo "run.sh: no build/config.st2; make install in fujinet-config studio2/" >&2; exit 1; }
        ./build.sh "$CLIENT"
    fi
    export FUJINET_BOOT="$HERE/build/$CLIENT.st2"
fi
mkdir -p build/snap build/nvram build/cfg
args=(studio2 -rompath "$ROMS" -snapshot_directory "$HERE/build/snap"
      -nvram_directory "$HERE/build/nvram" -cfg_directory "$HERE/build/cfg" -skip_gameinfo)

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle -seconds_to_run "${SECS:-120}")
else
    args+=(-window -nomax)
fi

cd "$MAME"
exec "$BIN" "${args[@]}"
