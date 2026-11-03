#!/usr/bin/env bash
# build-cart.sh -- build the Atari 2600 cartridge firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT if set; else build/config.bin (CONFIG from fujinet-config/atari-2600);
# else the fujidir stand-in, which ./build.sh makes.
CART_BOARD=fujivcs_rp2354a
CART_ELF=fujivcs
CART_BAKE="fujiconfigrom.h"

cart_gen() {
    local client=${FUJI_CLIENT:-}

    if [ -z "$client" ]; then
        client=build/config.bin
        [ -f "$client" ] || client=build/fujidir.bin
    fi
    [ -f "$client" ] || { echo "build-cart.sh: $client missing; run ./build.sh first" >&2; exit 1; }
    echo "build-cart.sh: baking $client"
    # (N+1) * 2K, with the claim 0xF0 from the end (FN_R_CLAIM in the fixed half)
    python3 ../common/tools/mkromh.py --blocks 2048 --claim=-0xF0 "$client" \
        > build/gen/fujiconfigrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
