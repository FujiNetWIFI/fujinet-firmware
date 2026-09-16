#!/usr/bin/env bash
# run.sh -- run a built client in MAME.
#
#   ./run.sh                 interactive, fujicfg
#   ./run.sh hello           interactive, a named client
#   ./run.sh hello --headless "HELLO FROM OS7"
#                            headless, print the screen, PASS/FAIL on a string
#   ./run.sh fujicfg --drive  headless, drive the controller through CONFIG and
#                            boot something; DRIVE_HOST/DRIVE_DOWN pick what
#
# The MAME tree needs emu/apply.sh run against it first for -cartslot fujinet;
# a plain image (no mailbox) runs on stock MAME.
set -euo pipefail
cd "$(dirname "$0")"

MAME_DIR=${MAME_DIR:-$HOME/Workspace/mame}
CLIENT=${1:-fujicfg}
BIN="$PWD/build/$CLIENT.bin"
[ -f "$BIN" ] || { echo "no $BIN -- run ./build.sh $CLIENT first" >&2; exit 1; }

SCRIPT="$PWD/emu/screen.lua"
[ "${2:-}" = "--drive" ] && SCRIPT="$PWD/emu/drive.lua"

# MAME resolves rompath, pluginspath and its Lua search path against its OWN
# working directory -- run it from anywhere else and -autoboot_script is
# silently ignored, with no error and no output. Everything we hand it is
# absolute for the same reason.
cd "$MAME_DIR"

if [ "${2:-}" = "--headless" ] || [ "${2:-}" = "--drive" ]; then
    if [ -n "${3:-}" ]; then export SCREEN_EXPECT="$3"; fi
    # Let the run outlast whatever the script is waiting for. screen.lua samples
    # at SCREEN_AT (measured from SCREEN_RESET_AT when a reset is asked for), so
    # a fixed window silently truncates the longer tests into failures.
    if [ "${2:-}" = "--drive" ]; then
        secs=${DRIVE_TIMEOUT:-90}
    else
        secs=$(( ${SCREEN_RESET_AT:-0} + ${SCREEN_AT:-2} + 6 ))
    fi
    exec ./mame coleco -cartslot fujinet -cart "$BIN" \
        -video none -sound none -nothrottle -seconds_to_run "$secs" \
        -autoboot_script "$SCRIPT"
fi

exec ./mame coleco -cartslot fujinet -cart "$BIN" -window -nomax
