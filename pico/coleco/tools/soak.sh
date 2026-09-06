#!/usr/bin/env bash
# soak.sh -- DBC-stream and boot every cartridge image in a ROM directory.
#
# Usage: tools/soak.sh [romdir] [limit]
#   default romdir: ~/Downloads/NoIntro-Coleco/Coleco - ColecoVision
#
# For every image (loose, or a zip member): drop it into the live fujinet-pc's
# host-0 root as /soak.rom, boot fujiboot in headless MAME, and let
# emu/soak.lua verify the whole served 32K window byte for byte against the
# mapping colmap plans for it. The pushed stream is also dumped
# (FUJINET_BOOTDUMP) and compared against the source file, so a truncated push
# cannot pass by accident. A per-image verdict lands in $SOAK_DIR/report.txt.
#
#   MAME_DIR      MAME tree with emu/apply.sh applied (default ~/Workspace/mame)
#   FUJINET_TCP   live fujinet-pc (default 127.0.0.1:9995 -- must be running)
#   SOAK_SD       the fujinet-pc host-0 root
#   SOAK_DIR      work dir (default build/soak)
#
# NOTE the extension: the images go in as .rom, not .col. The ESP32 decides
# MEDIATYPE_ROM by extension, and .col support is a change to
# lib/media/rs232/diskType.cpp that a long-running fujinet-pc built before it
# will not have.

set -euo pipefail
cd "$(dirname "$0")/.."

ROMDIR=${1:-$HOME/Downloads/NoIntro-Coleco/Coleco - ColecoVision}
LIMIT=${2:-0}
MAME_DIR=${MAME_DIR:-$HOME/Workspace/mame}
SOAK_SD=${SOAK_SD:-$HOME/Workspace/fujinet-pc-rs232/build/dist/SD}
SOAK_DIR=${SOAK_DIR:-$PWD/build/soak}
export FUJINET_TCP=${FUJINET_TCP:-127.0.0.1:9995}

mkdir -p "$SOAK_DIR/roms" "$SOAK_DIR/dump"

# 1. Flatten the corpus (spaces -> underscores). Loose files and zip members
#    are both accepted, and anything without a ColecoVision header magic is
#    skipped -- the No-Intro set includes the console BIOS itself, which is not
#    a cartridge and would simply hang the boot.
if [ -z "$(ls -A "$SOAK_DIR/roms" 2>/dev/null)" ]; then
    find "$ROMDIR" -maxdepth 1 -type f \( -iname '*.col' -o -iname '*.rom' \
        -o -iname '*.bin' \) | while read -r b; do
        cp "$b" "$SOAK_DIR/roms/$(basename "$b" | tr ' ' '_')"
    done
    find "$ROMDIR" -maxdepth 1 -type f -iname '*.zip' | while read -r z; do
        unzip -o -j -q "$z" -d "$SOAK_DIR/unz" 2>/dev/null || continue
    done
    if [ -d "$SOAK_DIR/unz" ]; then
        for f in "$SOAK_DIR/unz"/*; do
            [ -f "$f" ] || continue
            magic=$(head -c 2 "$f" | xxd -p)
            case "$magic" in
                55aa|aa55) cp "$f" "$SOAK_DIR/roms/$(basename "$f" | tr ' ' '_')" ;;
                *) echo "skip (no cartridge header): $(basename "$f")" ;;
            esac
        done
        rm -rf "$SOAK_DIR/unz"
    fi
fi

# 2. fujiboot, pointed at /soak.rom, built once.
BOOT_HOST=0 BOOT_PATH=/soak.rom ./build.sh fujiboot >/dev/null

: > "$SOAK_DIR/report.txt"
pass=0; fail=0; n=0

for rom in "$SOAK_DIR/roms"/*; do
    [ -f "$rom" ] || continue
    n=$((n + 1))
    if [ "$LIMIT" -gt 0 ] && [ "$n" -gt "$LIMIT" ]; then break; fi
    name=$(basename "$rom")

    cp "$rom" "$SOAK_SD/soak.rom"
    # 3. What should the console see? colmap's own answer, computed here so a
    #    bug in the cartridge cannot also define the expectation.
    python3 tools/expect.py "$rom" "$SOAK_DIR/expect.bin"

    out=$(cd "$MAME_DIR" && SOAK_EXPECT="$SOAK_DIR/expect.bin" \
        FUJINET_BOOTDUMP="$SOAK_DIR/dump/cur" \
        ./mame coleco -cartslot fujinet -cart "$OLDPWD/build/fujiboot.bin" \
            -video none -sound none -nothrottle -seconds_to_run 45 \
            -autoboot_script "$OLDPWD/emu/soak.lua" 2>&1 | grep '^soak:' || true)
    [ -z "$out" ] && out="soak: FAIL no verdict"

    # 4. The pushed stream must be the file, byte for byte.
    if [ -f "$SOAK_DIR/dump/cur.rom" ]; then
        cmp -s "$SOAK_DIR/dump/cur.rom" "$rom" || out="$out (STREAM MISMATCH)"
        rm -f "$SOAK_DIR/dump/cur.rom"
    else
        out="$out (NO STREAM DUMPED)"
    fi

    # No pipe here: `case ... | tee` would run the counters in a subshell and
    # every increment would be lost with it.
    if [ "$out" = "soak: PASS" ]; then
        pass=$((pass + 1)); line="PASS $name"
    else
        fail=$((fail + 1)); line="FAIL $name -- ${out#soak: }"
    fi
    echo "$line"
    echo "$line" >> "$SOAK_DIR/report.txt"
done

summary="---- $pass passed, $fail failed"
echo "$summary"
echo "$summary" >> "$SOAK_DIR/report.txt"
[ "$fail" -eq 0 ]
