#!/usr/bin/env bash
# soak.sh -- push every image in a corpus over the network, boot it, and
# byte-compare the served window against the file on disk.
#
#   SD=... FUJINET_TCP=host:port ./tools/soak.sh      the synthetic corpus
#   CORPUS=~/roms SD=... FUJINET_TCP=... ./tools/soak.sh   any .bin/.chf images
#
# SD (the fujinet-pc SD root) and FUJINET_TCP (its BoIP listener) have no
# defaults: fujinet-pc takes one BoIP client at a time.
#
# An image named *_errN.bin must be refused with FN_BOOT_ERR N.
#
# Resumable via build/soak.log; FRESH=1 starts over. Each image gets its own
# fujiboot build, since the boot path is baked into the client.
set -euo pipefail
cd "$(dirname "$0")/.."
HERE="$PWD"

MAME="${MAME:-$HOME/Workspace/mame}"
SD="${SD:?soak.sh: set SD to the fujinet-pc SD root}"
: "${FUJINET_TCP:?soak.sh: set FUJINET_TCP to the BoIP host:port of that fujinet-pc}"
export FUJINET_TCP
CORPUS="${CORPUS:-$HERE/build/soak}"
LOG="$HERE/build/soak.log"
SECS="${SECS:-120}"

[ -x "$MAME/mame" ] || { echo "soak.sh: no mame binary in $MAME" >&2; exit 1; }
[ -d "$SD" ] || { echo "soak.sh: no SD root at $SD" >&2; exit 1; }

if [ ! -d "$CORPUS" ]; then
    echo "soak.sh: generating the synthetic corpus"
    python3 tools/mkcorpus.py "$CORPUS" >/dev/null
fi

[ -n "${FRESH:-}" ] && rm -f "$LOG"
mkdir -p "$(dirname "$LOG")"
touch "$LOG"

pass=0; fail=0; skip=0
for img in "$CORPUS"/*.bin "$CORPUS"/*.chf; do
    [ -e "$img" ] || continue
    name=$(basename "$img")

    if grep -q "^$name " "$LOG" 2>/dev/null; then
        skip=$((skip + 1)); continue
    fi

    cp -f "$img" "$SD/$name"
    BOOT_PATH="/$name" ./build.sh fujiboot >/dev/null

    out=$( cd "$MAME" && BOOT_IMAGE="$img" \
        ./mame channelf -bios sl31253 -cartslot fujinet \
          -cart "$HERE/build/fujiboot.bin" \
          -autoboot_script "$HERE/emu/boottest.lua" \
          -video none -sound none -nothrottle -seconds_to_run "$SECS" 2>&1 || true )

    want_err=$(echo "$name" | sed -n 's/.*_err\([0-9]*\)\.[a-z]*$/\1/p')
    if [ -n "$want_err" ]; then
        if echo "$out" | grep -q "BOOTTEST: REFUSED err=$want_err\$"; then
            echo "$name PASS refused err=$want_err" >> "$LOG"
            pass=$((pass + 1))
            printf "  %-28s PASS (refused, err %s)\n" "$name" "$want_err"
        else
            echo "$name FAIL" >> "$LOG"
            fail=$((fail + 1))
            printf "  %-28s FAIL (want refused, err %s)\n" "$name" "$want_err"
            echo "$out" | grep -E "BOOTTEST|fujinet:" | sed 's/^/      /' | tail -6
        fi
    elif echo "$out" | grep -q "BOOTTEST: PASS"; then
        echo "$name PASS $(stat -c%s "$img") bytes" >> "$LOG"
        pass=$((pass + 1))
        printf "  %-28s PASS\n" "$name"
    else
        echo "$name FAIL" >> "$LOG"
        fail=$((fail + 1))
        printf "  %-28s FAIL\n" "$name"
        echo "$out" | grep -E "BOOTTEST|fujinet:" | sed 's/^/      /' | tail -6
    fi
    rm -f "$SD/$name"
done

total=$(grep -c PASS "$LOG" 2>/dev/null || echo 0)
echo
echo "soak: $pass passed, $fail failed, $skip already recorded this run"
echo "soak: $total/$(ls "$CORPUS" | wc -l) recorded in $LOG"
[ "$fail" -eq 0 ]
