# pico/template

The cartridge a new bring-up starts from: an 8K ROM window on an RP2354B, with pico/common
doing the FujiBus link, the mailbox protocol and the build. No console runs it.

## Layout
| File | Defines |
|---|---|
| `build-cart.sh` | the cart's settings for [common/](../common/)'s `scripts/build-cart.lib.sh`; with no `$FUJI_CLIENT`, it bakes a stand-in client of 8K of `$FF` carrying only the mailbox claim |
| `firmware/CMakeLists.txt` | `fujicart_firmware()`: the sources in link order, the USB product string, and `tmpl_core1_main` for the SRAM check |
| `firmware/boards/fujitemplate_rp2354b.{h,cmake}` | the board: RP2354B, 2 MB flash, and `FUJI_SYS_CLOCK_KHZ` |
| `firmware/include/tmpl_cart.h`, `firmware/src/tmpl_cart.c` | the pin map, and core1's bus loop: one access per `/CE` low |
| `firmware/include/tmplmap.h`, `firmware/src/tmplmap.c` | the image gate, plan and apply, and `tmplmap_decode()`, the connector-to-window decode core1 and a MAME device share |
| `firmware/include/fuji_mailbox.h` | where the mailbox sits in the window: status fields, reply slices, hotspot pages, the claim |
| `firmware/include/fuji_cart.h`, `firmware/src/fuji_cart.c` | what the two cores share: the hotspot ring, the served and staged windows, the swap flags |
| `firmware/include/fujinet.h`, `firmware/src/fujinet.c` | the cart's `fuji_port_ops_t`: publishing a byte, and receiving, planning and staging a pushed image |
| `firmware/include/fujiboot.h`, `firmware/src/fujiboot.c`, `firmware/src/main.c` | power-on: pins, clock, the baked client, core1, the USB loop |
| `firmware/baked/fujiconfigrom.h` | the stand-in client, so the firmware builds with no console toolchain |
| `firmware/host_test/` | `test_tmplmap`, and [common/](../common/)'s `test_fujimail` against this mailbox map |

## How it fits
- Every protocol, link and build piece comes from [common/](../common/); what is here is what
  differs from cart to cart.
- A new cart copies this directory and renames the `tmpl` prefix, then replaces the board, the
  pin map and bus loop, the mapper, the mailbox offsets and the client. It adds a MAME device
  whose `emu/apply.sh` uses `common/emu/graft.lib.sh`, as [arcadia/](../arcadia/)'s does.
- `tmpl_cart.c` assumes a select that pulses once per access. A console whose select stays low
  across accesses needs [arcadia/](../arcadia/)'s edge detector instead.

## Build
`./build-cart.sh` (or `--baked`) builds `build/fw-fujitemplate_rp2354b/fujitemplate.uf2`;
`make -C firmware/host_test` runs the host tests. The pico cartridges workflow does both. Not
part of the ESP32 or FujiNet-PC builds.
