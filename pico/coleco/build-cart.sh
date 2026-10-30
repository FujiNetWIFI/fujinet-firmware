#!/usr/bin/env bash
# build-cart.sh -- build the RP2040 cartridge firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT, else build/config.bin (real CONFIG, from fujinet-config's coleco/
# `make install`), else the fujicfg stand-in, else fujitest. The stand-ins are for bring-up
# only.
CART_BOARD=fujicoleco
CART_ELF=fujicoleco
CART_BAKE="fujiconfigrom.h"

cart_gen() {
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
    echo "build-cart.sh: baking build/$CLIENT.bin"
    python3 ../common/tools/mkromh.py --size 32768 --claim 0x7CFC "build/$CLIENT.bin" \
        > build/gen/fujiconfigrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
