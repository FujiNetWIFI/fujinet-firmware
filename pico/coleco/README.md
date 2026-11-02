# pico/coleco

FujiNet for the ColecoVision: an RP2040 cartridge that serves the console's 32K cart window,
every ColecoVision mapper and a mailbox made entirely of reads, linked over USB CDC to an
ESP32-S3 `fujiversal-coleco`.

## Layout
| Path | Defines |
|---|---|
| `firmware/include/fuji_mailbox.h` | the mailbox: the 1K reply at `$F800`, status fields, hotspot pages, the claim; register numbers and values come from [common/](../common/)'s `fuji_mailbox_proto.h` |
| `firmware/include/coleco_cart.h`, `firmware/src/coleco_cart.c` | the pin map, `coleco_serve()`, and core1's two-tier bus loop: serve off the address, act once the chip select confirms |
| `firmware/include/colmap.h`, `firmware/src/colmap.c` | flat, MegaCart, X-in-1, Activision and Opcode SGC: plan an image, the size gate, and the serve model core1 inlines and the MAME device runs |
| `firmware/include/fuji_cart.h`, `firmware/src/fuji_cart.c` | what the two cores share: the hotspot ring, the two 32K windows, staging and the swap flags |
| `firmware/include/fuji_store.h`, `firmware/src/fuji_store.c` | the push store: images up to 32K straight into the staging window, larger ones in 128K of SRAM; it replaces [common/](../common/)'s, which needs a flash tier |
| `firmware/include/fujinet.h`, `firmware/src/fujinet.c` | the cart's `fuji_port_ops_t`: publishing a byte, and receiving, planning and staging a pushed image |
| `firmware/include/fujiboot.h`, `firmware/src/fujiboot.c`, `firmware/src/main.c` | power-on: pins, clock, the baked client, core1, the USB loop and the console-power watch |
| `firmware/baked/fujiconfigrom.h` | the CONFIG served at power-on when there is no fresh `build/gen` (CI, the ESP32 build) |
| `firmware/boards/fujicoleco.{h,cmake}` | the board: a stock Pico, 250 MHz |
| `firmware/host_test/` | `test_colmap` (against MAME's own cartridge handlers), `test_busedge` (the two-tier loop), and [common/](../common/)'s `test_fujimail` against this mailbox map |
| `testrom/` | z88dk clients: `hello`, `fujitest`, `fujiboot`, the `fujicfg` stand-in CONFIG, and their display, input, sound, keyboard and mailbox library |
| `emu/` | the MAME cart device (`apply.sh` grafts it), `screen.lua` (screen as text), `drive.lua` (drives CONFIG) and `soak.lua` |
| `tools/` | `checkrom.py` (size, header, claim, clear mailbox pages), `expect.py` (the window a booted image should show, written apart from colmap), `soak.sh` |

## How it fits
- The FujiBus codec, the USB device, `fujimail.c`, the `.cfg` parser, `fuji_port.c`,
  `fuji_watchdog.c`, the build and the MAME graft steps come from [common/](../common/).
- The ESP32-S3 runs the `fujiversal-coleco` board; `discover_mediatype()` mounts `.col` as a ROM.
- The real CONFIG is fujinet-config's `coleco/`; its `make install` puts `build/config.bin`
  here for `./build-cart.sh --bake`.

## Build
- `./build.sh` builds the clients into `build/` (z88dk at `~/Workspace/z88dk` and os7lib at
  `~/Workspace/os7lib`; `Z88DK`, `OS7LIB` override). `./build-cart.sh` bakes `build/config.bin`
  (else `fujicfg`) into `build/gen` and builds `build/fw-fujicoleco/fujicoleco.uf2`;
  `--baked` builds from `firmware/baked/` alone, `--bake` refreshes it.
- `make -C firmware/host_test` runs the host tests. The pico cartridges workflow runs them and
  `--baked`.
- `emu/apply.sh ~/Workspace/mame`, then `make REGENIE=1 NOWERROR=1` in the MAME tree, grafts the
  device (`-cartslot fujinet`). Against a fujinet-pc on `$FUJINET_TCP`:
  `./run.sh fujitest --headless SSID`, `./run.sh fujiboot --headless`,
  `./run.sh config --drive` (with `DRIVE_SCRIPT`), `tools/soak.sh <romdir>`.

## Notes
- Edge: A0-A14, D0-D7, `/8000 /A000 /C000 /E000`, +5V, GND; no `/RD`, `/WR`, clock or reset.
  The selects come from a '138 gated by `/MREQ` and `/RFSH`, so a write looks like a read and
  refresh never selects. Every mapper but the SGC works from the address alone.
- A15 is missing, so console RAM at `$7C00-$7FFF` looks like the mailbox; core1 acts on hotspots
  and bank switches only once the select confirms the access.
- An image of more than 32K must be in SRAM to be served in time, so images cap at 128K: the
  256K and 512K MegaCarts and the X-in-1 images need an RP2350 board.
- The `.cfg` sibling's `mapper=` (`flat`, `megacart`, `xin1`, `activision`, `sgc`) is the only way
  to tell the 64K Activision and 128K SGC carts from MegaCarts of the same size.
- The vblank interrupt is the Z80's `/NMI`: a client's handler must never read `$F800` and up,
  and the swap stub turns the interrupt off before it swaps.
- sccz80 drops a discarded volatile read and mis-indexes `FN_REPLY[...]`: every mailbox read
  stores to a volatile sink, and the reply is read through a pointer variable.
- The power watch reboots the cart, back to CONFIG, when the console's 5V goes away: the
  console's RESET doesn't reach the cart, and a booted game has ended the mailbox.
- No cartridge board exists; everything here ran in host tests and MAME only.
