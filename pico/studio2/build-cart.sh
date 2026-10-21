#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT if set; else build/config.st2 (the real CONFIG from
# fujinet-config/studio2); else fujitest; else hello.
CART_BOARD=fujistudio2_rp2354b
CART_ELF=fujistudio2
CART_BAKE="fujiconfigrom.h"

cart_gen() {
    if [ -n "${FUJI_CLIENT:-}" ]; then
        CLIENT=$FUJI_CLIENT
    elif [ -f build/config.st2 ]; then
        CLIENT=config
    else
        CLIENT=fujitest
        [ -f build/fujitest.st2 ] || CLIENT=hello
    fi
    [ -f "build/$CLIENT.st2" ] || { echo "build-cart.sh: build/$CLIENT.st2 missing; run ./build.sh first" >&2; exit 1; }
    echo "build-cart.sh: baking build/$CLIENT.st2"
    python3 tools/checkcfg.py "build/$CLIENT.st2"
    python3 ../common/tools/mkromh.py "build/$CLIENT.st2" > build/gen/fujiconfigrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
