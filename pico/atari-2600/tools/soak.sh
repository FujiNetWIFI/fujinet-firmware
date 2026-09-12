#!/usr/bin/env bash
# soak.sh -- push, boot and byte-compare the whole cartridge corpus.
#
#   tools/soak.sh [outdir]     default: the fujinet-pc SD host
#
# For every image: build a boot client aimed at it, push it over the network,
# swap it in, and compare EVERY served bank against the file. Resumable --
# results land in build/soak/ one file per image, and an image already marked
# PASS is skipped, so an interrupted run picks up where it stopped.
#
# fujinet-pc's BoIP listener takes one client, so this is strictly serial.

set -uo pipefail
cd "$(dirname "$0")/.."

SD=${1:-$HOME/Workspace/fujinet-pc-rs232/build/dist/SD}
RES=build/soak
mkdir -p "$RES"

python3 tools/mkcorpus.py "$SD" >/dev/null

declare -A SCHEME=(
  [soak2k]=FLAT [soak4k]=FLAT
  [soakf8]=F8   [soakf8sc]=F8SC
  [soakfa]=FA
  [soakf6]=F6   [soakf6sc]=F6SC
  [soakf4]=F4   [soakf4sc]=F4SC
  [soake0]=E0   [soakua]=UA [soakfe]=FE [soakcv]=CV
)

pass=0; fail=0; skip=0
for name in soak2k soak4k soakf8 soakf8sc soakfa soakf6 soakf6sc \
            soakf4 soakf4sc soake0 soakua soakfe soakcv; do
    if grep -q PASS "$RES/$name" 2>/dev/null; then
        skip=$((skip+1)); continue
    fi
    BOOT_PATH="/$name.bin" ./build.sh fujiboot >/dev/null 2>&1 || {
        echo "FAIL $name (client build)" | tee "$RES/$name"; fail=$((fail+1)); continue; }
    log=$(BOOT_IMAGE="$SD/$name.bin" SOAK_SCHEME="${SCHEME[$name]}" \
          FUJINET_DEBUG=1 SLOT=fujinet SECS=60 ./run.sh fujiboot soaktest 2>&1)
    out=$(printf '%s\n' "$log" | grep -E "^(PASS|FAIL)" | head -1)
    [ -z "$out" ] && out="FAIL $name: no verdict (timeout?)"

    # Which board the cartridge actually chose, not which one the harness
    # drove. Without this an 8K E0 image served as F8 could still be scored by
    # F8 rules and "pass" -- the whole reason the .cfg sibling exists.
    # Anchored on the SERVING line. There is a second "mapper" line -- the one
    # that echoes the .cfg -- and it names the scheme without the SC suffix,
    # so an unanchored grep scores every Super Chip image as a miss.
    got=$(printf '%s\n' "$log" | sed -n 's/.*as a game, mapper \([A-Z0-9]*\),.*/\1/p' | head -1)
    if [ -z "$got" ]; then
        out="FAIL $name: the image never became a game"
    elif [ "$got" != "${SCHEME[$name]}" ]; then
        out="FAIL $name: served as $got, want ${SCHEME[$name]}"
    fi
    echo "$out" > "$RES/$name"
    printf '%-10s %s\n' "$name" "$out"
    case "$out" in PASS*) pass=$((pass+1));; *) fail=$((fail+1));; esac
done

echo
echo "soak: $pass passed, $fail failed, $skip already passing"
[ "$fail" -eq 0 ]
