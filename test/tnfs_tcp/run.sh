#!/usr/bin/env bash
# Build and run the TNFS-over-TCP framing test:
#   1. a baseline run straight at the upstream server, then
#   2. a run through a fragmenting proxy that chops the server->client stream
#      into small pieces, reproducing the WAN segmentation that used to desync
#      the client (timeouts / out-of-sequence).
#
# usage: run.sh [upstream_host] [upstream_port]      (default 127.0.0.1 16384)
# env:   FRAG=<bytes/piece, default 3>  FRAGDELAY=<seconds, default 0.004>
#        PROXY_PORT=<local proxy port, default 16599>
set -uo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UP_HOST="${1:-127.0.0.1}"
UP_PORT="${2:-16384}"
PROXY_PORT="${PROXY_PORT:-16599}"

"$DIR/build.sh"

echo
echo "### baseline: direct to $UP_HOST:$UP_PORT"
"$DIR/tnfs_tcp_test" "$UP_HOST" "$UP_PORT"
BASE_RC=$?

echo
echo "### fragmented: via proxy (FRAG=${FRAG:-3}B DELAY=${FRAGDELAY:-0.004}s) -> $UP_HOST:$UP_PORT"
FRAG="${FRAG:-3}" FRAGDELAY="${FRAGDELAY:-0.004}" \
    python3 "$DIR/frag_proxy.py" "$PROXY_PORT" "$UP_HOST" "$UP_PORT" >/tmp/tnfs_frag_proxy.log 2>&1 &
PXP=$!
trap 'kill $PXP 2>/dev/null' EXIT
sleep 0.5
"$DIR/tnfs_tcp_test" 127.0.0.1 "$PROXY_PORT"
FRAG_RC=$?

echo
echo "=== baseline rc=$BASE_RC  fragmented rc=$FRAG_RC  (0 = PASS) ==="
[ "$BASE_RC" = 0 ] && [ "$FRAG_RC" = 0 ]
