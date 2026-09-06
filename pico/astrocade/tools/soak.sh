#!/usr/bin/env bash
# soak.sh -- DBC-stream and boot every cart image in a ROM directory.
#
# Usage: tools/soak.sh [romdir]     default: ~/Workspace/mame/roms/astrocde
#
# For every .bin inside every zip (and every loose .bin): drop it into the
# live fujinet-pc's host-0 root as /soak.bin, boot fujiboot in headless
# MAME, and let emu/soak.lua verify the whole served window byte-for-byte
# against the expected astromap mapping after the swap (mirror/pad rules).
# The pushed stream is also dumped (FUJINET_BOOTDUMP) and byte-compared
# against the source file. A per-image verdict lands in $SOAK_DIR/report.txt.
#
#   MAME_DIR      MAME tree with the device applied (default ~/Workspace/mame)
#   FUJINET_TCP   live fujinet-pc (default 127.0.0.1:9995 -- must be running)
#   SOAK_SD       the fujinet-pc host-0 root
#                 (default ~/Workspace/fujinet-pc-rs232/build/dist/SD)
#   SOAK_DIR      work dir (default build/soak)

set -euo pipefail
cd "$(dirname "$0")/.."

ROMDIR=${1:-$HOME/Workspace/mame/roms/astrocde}
MAME_DIR=${MAME_DIR:-$HOME/Workspace/mame}
SOAK_SD=${SOAK_SD:-$HOME/Workspace/fujinet-pc-rs232/build/dist/SD}
SOAK_DIR=${SOAK_DIR:-$PWD/build/soak}
export FUJINET_TCP=${FUJINET_TCP:-127.0.0.1:9995}

mkdir -p "$SOAK_DIR/roms" "$SOAK_DIR/dump"

# 1. Flatten the corpus: zip members become <zipstem>_<member>.
if [ -z "$(ls -A "$SOAK_DIR/roms" 2>/dev/null)" ]; then
    for z in "$ROMDIR"/*.zip; do
        stem=$(basename "$z" .zip)
        tmp=$(mktemp -d)
        unzip -qq "$z" -d "$tmp"
        find "$tmp" -type f | while read -r f; do
            cp "$f" "$SOAK_DIR/roms/${stem}_$(basename "$f" | tr ' ' _)"
        done
        rm -rf "$tmp"
    done
    for b in "$ROMDIR"/*.bin; do
        [ -e "$b" ] && cp "$b" "$SOAK_DIR/roms/$(basename "$b" | tr ' ' _)"
    done 2>/dev/null || true
fi

# 2. fujiboot aimed at the fixed name, built once.
BOOT_HOST=0 BOOT_PATH=/soak.bin ./build.sh fujiboot >/dev/null

expected() {    # the astromap FLAT mapping: mirror power-of-two, pad odd
    python3 - "$1" "$2" <<'EOF'
import sys
img = open(sys.argv[1], "rb").read()
out = bytearray(8192)
if img and (len(img) & (len(img) - 1)) == 0 and len(img) < 8192:
    for a in range(8192):
        out[a] = img[a % len(img)]
else:
    out[:len(img)] = img
    out[len(img):] = b"\xFF" * (8192 - len(img))
open(sys.argv[2], "wb").write(out)
EOF
}

# Resumable: a PASS already in the report stands; FAIL lines are retried.
report="$SOAK_DIR/report.txt"
touch "$report"
grep -v ' FAIL' "$report" > "$report.tmp" && mv "$report.tmp" "$report" || true
pass=0; fail=0; total=0

for rom in "$SOAK_DIR/roms"/*; do
    name=$(basename "$rom")
    total=$((total + 1))
    if grep -qF "$(printf '%-40s' "$name") PASS" "$report"; then
        pass=$((pass + 1))
        continue
    fi
    cp "$rom" "$SOAK_SD/soak.bin"
    expected "$rom" "$SOAK_DIR/expect.bin"
    verdict=$(SOAK_EXPECT="$SOAK_DIR/expect.bin" \
        FUJINET_BOOTDUMP="$SOAK_DIR/dump/$name" \
        "$MAME_DIR/mame" astrocde -rompath "$MAME_DIR/roms" \
            -cartslot fujinet -cart "$PWD/build/fujiboot.bin" \
            -autoboot_script emu/soak.lua \
            -video none -sound none -nothrottle -seconds_to_run 90 \
            2>/dev/null | grep '^soak:' || echo "soak: FAIL no verdict")
    if [ -f "$SOAK_DIR/dump/$name.rom" ] \
       && ! cmp -s "$rom" "$SOAK_DIR/dump/$name.rom"; then
        verdict="soak: FAIL stream dump differs from source"
    fi
    case "$verdict" in
        "soak: PASS"*) pass=$((pass + 1)) ;;
        *)             fail=$((fail + 1)) ;;
    esac
    printf '%-40s %s\n' "$name" "${verdict#soak: }" | tee -a "$report"
done

echo "soak.sh: $pass/$total passed ($fail failed); report: $report"
[ "$fail" -eq 0 ]
