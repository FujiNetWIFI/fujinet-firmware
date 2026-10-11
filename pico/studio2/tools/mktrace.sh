#!/usr/bin/env bash
# mktrace.sh -- record the bus traces host_test/test_cycles replays.
#
#   tools/mktrace.sh [client ...]       default: fujitest, fujiboot, and config
#                                       if built (fujiboot boots whatever
#                                       BOOT_PATH it was built with)
#
# Runs each client in MAME against the live fujinet-pc ($FUJINET_TCP) under
# emu/s2trace.lua, and writes build/traces/<client>.trace: a "# sha1" line
# naming the copy of the image it ran (build/traces/<client>.st2; test_cycles
# refuses a trace whose image no longer matches), the reads, and an
# "X dev cmd nparam txlen" line per transaction the device ran. Re-run it
# after changing a client.
set -euo pipefail
cd "$(dirname "$0")/.."

clients=("$@")
if [ ${#clients[@]} -eq 0 ]; then
    clients=(fujitest fujiboot)
    [ -f build/config.st2 ] && clients+=(config)
fi
mkdir -p build/traces
for c in "${clients[@]}"; do
    [ -f "build/$c.st2" ] || ./build.sh "$c"
    t=build/traces/$c.trace
    raw=build/traces/$c.reads
    log=build/traces/$c.log
    until=${S2TRACE_UNTIL:-}
    [ -z "$until" ] && [ "$c" = fujitest ] && until="SEQ "
    [ -z "$until" ] && [ "$c" = fujiboot ] && until="SWAP"
    [ -z "$until" ] && until="${CFG_UNTIL:-/studio2}"
    S2TRACE="$PWD/$raw" S2TRACE_UNTIL="$until" FUJINET_DEBUG=1 SECS=120 \
        ./run.sh "$c" s2trace > "$log" 2>&1 || true
    cp "build/$c.st2" "build/traces/$c.st2"
    {
        echo "# sha1 $(sha1sum "build/traces/$c.st2" | cut -d' ' -f1) $c.st2"
        cat "$raw"
        sed -n 's/^fujinet: dev=\([0-9A-F]*\) cmd=\([0-9A-F]*\) nparam=\([0-9]*\) txlen=\([0-9]*\).*/X \1 \2 \3 \4/p' "$log"
    } > "$t"
    rm -f "$raw"
    echo "mktrace: $t: $(grep -c '^[FMD] ' "$t") reads, $(grep -c '^X ' "$t") transactions"
done
