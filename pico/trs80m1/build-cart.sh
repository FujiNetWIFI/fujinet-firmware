#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B TRS-80 Model I card firmware: [--baked | --bake] [board],
# as pico/common/scripts/build-cart.lib.sh describes. Nothing is baked: CONFIG boots from
# slot 1 as a disk image.
CART_BOARD=fujitrs80m1_rp2354b
CART_ELF=fujitrs80m1
CART_BAKE=""

cart_gen() {
    :
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
