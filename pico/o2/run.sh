#!/bin/bash
# Run a cart under the FujiNet-patched o2em against a live fujinet-pc-rs232.
#
#   ./run.sh fujitest                 windowed, interactive
#   ./run.sh fujitest -frames=180 -dumpscr=/tmp/s.ppm -dumptxt=1
#
# fujinet-pc-rs232 must be running with [BOIP] enabled (port 9995 by default).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
O2EM="${O2EM:-$HOME/Workspace/o2em/o2em}"
FUJI="${FUJI:-127.0.0.1:9995}"
CART="${1:-fujitest}"; shift || true
exec "$O2EM" -romdir="$HERE/build/" -biosdir="$HERE/bios/" -nosound \
     -fujinet="$FUJI" -fujinet-debug=1 "$@" "$CART.bin"
