#!/usr/bin/env bash
# Build and run the harness through zombie_proxy.py, which models a real NAT/
# firewall idle drop: when the connection stalls mid-session the upstream socket
# is kept open, so the server's session stays bound to the dead connection (the
# client's FIN never arrived). The client reconnects, but the server rejects the
# new connection ("Session is assigned to another TCP connection") until it reaps
# the zombie -- UNLESS the server supports session migration.
#
# Expected: FAIL against a stock tnfsd; PASS against one with the TCP session
# migration fix (FujiNetWIFI/tnfsd).
#
# usage: run_zombie.sh [upstream_host] [upstream_port]   (default 127.0.0.1 16384)
set -uo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UP_HOST="${1:-127.0.0.1}"
UP_PORT="${2:-16384}"
PROXY_PORT="${PROXY_PORT:-16597}"

"$DIR/build.sh"

STALL_AFTER="${STALL_AFTER:-20}" \
    python3 "$DIR/zombie_proxy.py" "$PROXY_PORT" "$UP_HOST" "$UP_PORT" >/tmp/tnfs_zombie_proxy.log 2>&1 &
PXP=$!
trap 'kill $PXP 2>/dev/null' EXIT
for _ in $(seq 1 50); do grep -q "zombie-proxy :" /tmp/tnfs_zombie_proxy.log && break; sleep 0.1; done

echo "### session-migration test -> $UP_HOST:$UP_PORT"
"$DIR/tnfs_tcp_test" 127.0.0.1 "$PROXY_PORT"
