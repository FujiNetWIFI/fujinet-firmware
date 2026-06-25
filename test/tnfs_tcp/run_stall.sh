#!/usr/bin/env bash
# Build and run the harness through stall_proxy.py, which lets the session start
# normally and then black-holes the connection mid-session (mount succeeds, then
# a later request gets no response) -- the "connection went stale during idle"
# case. A correct client drops the dead connection on timeout and reconnects;
# the run should end in PASS.
#
# usage: run_stall.sh [upstream_host] [upstream_port]   (default 127.0.0.1 16384)
# env:   STALL_AFTER=<server->client bytes before black-holing, default 20>
#        PROXY_PORT=<local proxy port, default 16598>
set -uo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UP_HOST="${1:-127.0.0.1}"
UP_PORT="${2:-16384}"
PROXY_PORT="${PROXY_PORT:-16598}"

"$DIR/build.sh"

STALL_AFTER="${STALL_AFTER:-20}" \
    python3 "$DIR/stall_proxy.py" "$PROXY_PORT" "$UP_HOST" "$UP_PORT" >/tmp/tnfs_stall_proxy.log 2>&1 &
PXP=$!
trap 'kill $PXP 2>/dev/null' EXIT
for _ in $(seq 1 50); do grep -q "stall-proxy :" /tmp/tnfs_stall_proxy.log && break; sleep 0.1; done

echo "### stale-connection / reconnect test -> $UP_HOST:$UP_PORT"
"$DIR/tnfs_tcp_test" 127.0.0.1 "$PROXY_PORT"
