#!/usr/bin/env bash
# build-cart.sh -- build the RP2040 cartridge firmware.
#
#   ./build-cart.sh [board]           bake build/<client>.bin into build/gen and build
#                                     (default board: fujiarcadia)
#   ./build-cart.sh --baked [board]   build from firmware/baked/ alone: no assembler
#                                     needed (CI, the ESP32 build)
#   ./build-cart.sh --bake [board]    as the first form, then refresh firmware/baked/
#                                     from build/gen
#
# Client: $FUJI_CLIENT if set; else build/config.bin -- the REAL CONFIG from
# fujinet-config/arcadia (copy its build/config.bin here, or run its `make rom.h`); else
# the fujicfg testrom stand-in; else fujitest. The stand-ins exist for bring-up only; a
# release cart bakes real CONFIG.
set -euo pipefail
cd "$(dirname "$0")"

MODE=gen
case "${1:-}" in
    --baked) MODE=baked; shift ;;
    --bake)  MODE=bake; shift ;;
esac
BOARD=${1:-fujiarcadia}
GEN=$PWD/build/gen

if [ "$MODE" = baked ]; then
    GEN=$PWD/build/none                 # firmware/baked/ is next on the path
else
    if [ -n "${FUJI_CLIENT:-}" ]; then
        CLIENT=$FUJI_CLIENT
    elif [ -f build/config.bin ]; then
        CLIENT=config
    else
        CLIENT=fujicfg
    fi

    if [ ! -f "build/$CLIENT.bin" ]; then
        if [ "$CLIENT" = fujicfg ] && [ -f build/fujitest.bin ]; then
            echo "build-cart.sh: no fujicfg.bin yet; baking fujitest instead" >&2
            CLIENT=fujitest
        else
            echo "build-cart.sh: build/$CLIENT.bin missing; run ./build.sh first" >&2
            exit 1
        fi
    fi
    mkdir -p build/gen
    echo "build-cart.sh: baking build/$CLIENT.bin"
    python3 tools/mkromh.py "build/$CLIENT.bin" > build/gen/fujiconfigrom.h
fi

cmake -S firmware -B "firmware/build-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD" -DFUJI_GEN="$GEN"
ninja -C "firmware/build-$BOARD"

if [ "$MODE" = bake ]; then
    mkdir -p firmware/baked
    cp build/gen/fujiconfigrom.h firmware/baked/
    echo "build-cart.sh: firmware/baked/ refreshed from build/$CLIENT.bin"
fi
echo "build-cart.sh: firmware/build-$BOARD/fujiarcadia.uf2"
