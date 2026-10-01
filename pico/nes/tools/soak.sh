#!/usr/bin/env bash
# soak.sh -- boot every synthetic corpus image through the loader ROM in MAME
# and check each mapper's banking against emu/maptest.lua's expectations.
#
# Usage: tools/soak.sh [corpus-dir]      default build/soak (tools/mkcorpus.py)
#
# Each image is sent through the loader (FUJINET_LOADER=1), as the cart would
# at power-on, so the soak also proves the copy path at every size. fujinet-pc
# is not involved: nothing here touches the network.
set -euo pipefail
cd "$(dirname "$0")/.."
DIR="${1:-build/soak}"
[ -d "$DIR" ] || python3 tools/mkcorpus.py "$DIR"
MAME="${MAME:-$HOME/Workspace/mame}"
pass=0; fail=0; failed=()
for img in "$DIR"/soak_*.nes; do
    name=$(basename "$img" .nes)
    mapper=$(python3 -c "import sys;h=open(sys.argv[1],'rb').read(16);print((h[6]>>4)|(h[7]&0xF0))" "$img")
    prg16=$(python3 -c "import sys;print(open(sys.argv[1],'rb').read(16)[4])" "$img")
    chr8=$(python3 -c "import sys;print(open(sys.argv[1],'rb').read(16)[5])" "$img")
    out=$(cd "$MAME" && FUJINET_LOADER=1 NES_EMU_DIR="$PWD/../fujinet-firmware/pico/nes/emu" \
          MAPTEST_MAPPER=$mapper MAPTEST_PRG16=$prg16 MAPTEST_CHR8=$chr8 \
          ./mame nes -nes_slot fujinet -cart "$OLDPWD/$img" -video none -sound none -nothrottle \
          -seconds_to_run 120 -autoboot_script "$OLDPWD/emu/maptest.lua" 2>&1 || true)
    if echo "$out" | grep -q "^PASS:"; then
        pass=$((pass + 1)); echo "PASS $name: $(echo "$out" | grep '^PASS:' | head -1)"
    else
        fail=$((fail + 1)); failed+=("$name"); echo "FAIL $name"; echo "$out" | grep -E "FAIL|fujinet:" | head -10
    fi
done
echo "soak: $pass passed, $fail failed ${failed[*]:-}"
[ $fail -eq 0 ]
