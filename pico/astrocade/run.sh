#!/usr/bin/env bash
# run.sh -- run a built client in MAME against a live fujinet-pc.
#
# Usage: ./run.sh [client] [extra mame args...]     default client: fujitest
#
#   FUJINET_TCP=host:port   fujinet-pc BoIP listener (default 127.0.0.1:9995)
#   FUJINET_DEBUG=1         per-transaction stderr log (default on here)
#   FUJINET_BOOTDUMP=prefix write pushed streams to prefix.rom/.cfg
#   MAME_DIR=path           MAME tree with the fujinet device applied
#
# Expects the astrocde BIOS in $MAME_DIR/roms and the device grafted in by
# ./emu/apply.sh (see README.md).

set -euo pipefail
cd "$(dirname "$0")"

MAME_DIR=${MAME_DIR:-$HOME/Workspace/mame}
CLIENT=${1:-fujitest}
[ $# -gt 0 ] && shift

[ -f "build/$CLIENT.bin" ] || ./build.sh "$CLIENT"

export FUJINET_TCP=${FUJINET_TCP:-127.0.0.1:9995}
export FUJINET_DEBUG=${FUJINET_DEBUG:-1}

exec "$MAME_DIR/mame" astrocde \
    -rompath "$MAME_DIR/roms" \
    -cartslot fujinet -cart "$PWD/build/$CLIENT.bin" \
    -window -nomax \
    "$@"
