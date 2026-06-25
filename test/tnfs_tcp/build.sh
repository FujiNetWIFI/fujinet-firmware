#!/usr/bin/env bash
# Build the standalone TNFS-over-TCP framing test harness.
# It compiles the REAL tnfslib + fnTcpClient from the repo (so it always tests
# the current code), with thin stubs for the firmware-only deps fnSystem/bus/fnUDP.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$DIR/../.." && pwd)"

g++ -std=c++17 -O2 -pthread -D__PC_BUILD_DEBUG__ \
  -I"$DIR/stubs" \
  -I"$REPO/lib/TNFSlib" -I"$REPO/lib/tcpip" -I"$REPO/lib/compat" \
  -I"$REPO/lib/utils" -I"$REPO/include" \
  "$DIR/tnfs_tcp_test.cpp" \
  "$DIR/stubs/stubs.cpp" \
  "$REPO/lib/TNFSlib/tnfslib.cpp" \
  "$REPO/lib/TNFSlib/tnfslibMountInfo.cpp" \
  "$REPO/lib/tcpip/fnTcpClient.cpp" \
  "$REPO/lib/tcpip/fnDNS.cpp" \
  -x c++ "$REPO/lib/compat/compat_inet.c" \
  -o "$DIR/tnfs_tcp_test"

echo "built $DIR/tnfs_tcp_test"
