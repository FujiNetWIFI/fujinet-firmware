#!/usr/bin/env bash
# Build Channel F console clients with Macroassembler AS (CPU F3850).
#
# AS is the same assembler the Arcadia (2650) and O2 (8048) ports already use,
# so CI reuses their cache block verbatim. Usage: ./build.sh [client ...]
set -euo pipefail

cd "$(dirname "$0")"
ASL="${ASL:-$HOME/asl/asl}"
P2BIN="${P2BIN:-$HOME/asl/p2bin}"
ROM_BASE=0x800
ROM_END="${ROM_END:-0xfff}"          # 2K default; larger clients override

[ -x "$ASL" ]   || { echo "build.sh: no assembler at $ASL" >&2; exit 1; }
[ -x "$P2BIN" ] || { echo "build.sh: no p2bin at $P2BIN" >&2; exit 1; }

mkdir -p build
python3 tools/mkfont.py

clients=("$@")
[ ${#clients[@]} -eq 0 ] && clients=(hello)

for c in "${clients[@]}"; do
    echo "=== $c ==="
    ( cd testrom && "$ASL" "$c.asm" -L -i . -q )
    # -l 0xff fills the gaps; note p2bin's -f FILTERS records out and would
    # silently produce an empty image, and -s would append a checksum byte.
    "$P2BIN" "testrom/$c.p" "build/$c.bin" -r "$ROM_BASE-$ROM_END" -l 0xff -q
    mv -f "testrom/$c.lst" "build/$c.lst" 2>/dev/null || true
    rm -f "testrom/$c.p"
    python3 tools/checkrom.py "build/$c.bin"
done
