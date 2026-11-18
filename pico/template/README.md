# pico/template

The cartridge a new bring-up starts from: an 8K ROM window on an RP2354B, with pico/common
doing the FujiBus link, the mailbox protocol and the build. No console runs it.

## How it fits
- Every protocol, link and build piece comes from [common/](../common/); what is here is what
  differs from cart to cart.
- A new cart copies this directory, as `docs/pico-cart-bringup.md` describes: it renames the
  `tmpl` prefix, then replaces the board, the pin map and bus loop, the mapper, the mailbox
  offsets and the client. It adds a MAME device whose `emu/apply.sh` uses
  `common/emu/graft.lib.sh`, as [arcadia/](../arcadia/)'s does.
- `tmpl_cart.c` assumes a select that pulses once per access. A console whose select stays low
  across accesses needs [arcadia/](../arcadia/)'s edge detector instead.

## Build
`./build-cart.sh` (or `--baked`) builds `build/fw-fujitemplate_rp2354b/fujitemplate.uf2`;
`make -C firmware/host_test` runs the host tests. The pico cartridges workflow does both. Not
part of the ESP32 or FujiNet-PC builds.

## Notes
- `firmware/baked/fujiconfigrom.h` is a stand-in client, so the firmware builds with no console
  toolchain. With no `$FUJI_CLIENT`, `build-cart.sh` bakes a stand-in of 8K of `$FF` carrying
  only the mailbox claim.
