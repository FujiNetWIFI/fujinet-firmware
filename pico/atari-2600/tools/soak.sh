#!/usr/bin/env bash
# soak.sh -- push, boot and byte-compare every bank of the cartridge corpus.
#
#   tools/soak.sh [outdir]     default: the fujinet-pc SD host
#
# Resumable: results go to build/soak/, and an image marked PASS is skipped.
# Serial, because fujinet-pc's BoIP listener takes one client.

set -uo pipefail
cd "$(dirname "$0")/.."

# Not the SD root: 26 files there would push other tests' targets off fujidir's
# one unpaged page of 14.
SD=${1:-$HOME/Workspace/fujinet-pc-rs232/build/dist/SD}
CORPUS=$SD/SOAK
RES=build/soak
mkdir -p "$RES"

python3 tools/mkcorpus.py "$CORPUS" >/dev/null

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
    BOOT_PATH="/SOAK/$name.bin" ./build.sh fujiboot >/dev/null 2>&1 || {
        echo "FAIL $name (client build)" | tee "$RES/$name"; fail=$((fail+1)); continue; }
    log=$(BOOT_IMAGE="$CORPUS/$name.bin" SOAK_SCHEME="${SCHEME[$name]}" \
          FUJINET_DEBUG=1 SLOT=fujinet SECS=60 ./run.sh fujiboot soaktest 2>&1)
    out=$(printf '%s\n' "$log" | grep -E "^(PASS|FAIL)" | head -1)
    [ -z "$out" ] && out="FAIL $name: no verdict (timeout?)"

    # The board the cart chose, from the serving line; the .cfg echo line names
    # the scheme without its SC suffix.
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
