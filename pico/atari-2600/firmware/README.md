# pico/atari-2600/firmware

The FujiNet Atari 2600 cartridge: an RP2354A on the Fujiversal-Atari2600 Rev1 board serving the
console's 4K window, with [pico/common/](../../common/) doing the FujiBus link to the ESP32-S3,
the mailbox protocol and the build. It sits beside gtortone's PlusCart-Pico import in
[pico/atari-2600/](../), which it borrows the bus technique and mapper set from and leaves
untouched.

## How it fits
- core1 serves the bus from SRAM with interrupts off; everything slower (rendering, the
  mailbox, the USB link) is core0's.
- The console writes to the cart, with no R/W line: the cart parks on the stable address and
  keeps the second-to-last data sample. Only STA/STX/STY may target a write page;
  `../tools/checkrom.py` enforces it on every client.
- The console's power switch is the way back to CONFIG: VSENSE low for 10 ms resets core1 and
  re-serves the baked client, and core1 never drives D0-D7 while the console is off.
- The ESP32-S3 reflashes the cart through BOOTSEL (1200 baud on the CDC line, or its RUN and
  BOOTSEL pins).
- The 4K window holds the mailbox's banks, reply slices, status page and text planes; two of its
  hotspot pages are synthetic, decoded by core1 outside the window.
- The console has no framebuffer, so the cart composes text, cards and boards into the 48-pixel
  kernel's planes (`src/vcs_render.c`).
- The ESP32 side is the `fujiversal-atari2600` board ([lib/bus/rs232/](../../../lib/bus/rs232/)
  over USB CDC).
- Clients mirror `fuji_mailbox.h` by hand; `../tools/checkdefs.py` compares them, following its
  include of `pico/common/include/fuji_mailbox_proto.h`. fujinet-config and the game ports call
  it from their own builds.

## Build
`../build-cart.sh` builds `../build/fw-fujivcs_rp2354a/fujivcs.uf2`; it bakes
`../build/config.bin` if present, else `../build/fujidir.bin`; `--baked` builds from `baked/` alone,
and `--bake` refreshes `baked/`, recording the client's repo and commit from `FUJI_CLIENT_ORIGIN`.
`make -C host_test` runs the host tests, including the write-sampling model MAME cannot run.
`../build.sh` assembles the 6502 test clients in `../testrom/` (Macro Assembler AS). The pico
cartridges workflow does all three. Not part of the ESP32 or FujiNet-PC builds.

## Notes
- Not yet run on hardware. Emulation: `../emu/apply.sh <mame-tree>`, then
  `SLOT=fujinet ../run.sh <client> <harness>` against a fujinet-pc on 127.0.0.1:9995.
- MAME's `readv_u8` is not side-effect free: a harness that sweeps a banked window trips its own
  hotspots, so compare whole-window dumps only for flat images.
