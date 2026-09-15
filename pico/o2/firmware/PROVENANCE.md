# Provenance of `pico/o2/firmware`

## What is vendored here

`VSC-PicoPAC/` from [aotta/PicoPAC](https://github.com/aotta/PicoPAC) at
`f0b3ebcd` (2025-08-11) — the C firmware only, flat at the root of this
directory. FujiNet's own additions live in `src/`, `include/` and `host_test/`.

Deliberately **not** vendored:

- `Kicad/` — contains `videopac-edgeconnector.lib` and `idt7006pf-pn64.lib`,
  md5-identical to [bwack/Videopac_USBCART](https://github.com/bwack/Videopac_USBCART)
  (LGPL-3.0), carried in with bwack's library nickname still embedded. We do not
  need them, and the Intellivision port left the equivalent PCB question
  unresolved (see `../../intellivision/PROVENANCE.md`).
- `selectgame*` — the menu ROM, derived from
  [wilco2009/Videopac-micro-SD-Cart](https://github.com/wilco2009/Videopac-micro-SD-Cart)
  (GPL-3.0). We have our own console client, so it is not needed.
- Gerbers and 3D-print files, for the same reason as the KiCad.

Taking only the C firmware keeps this to aotta's own work plus the A8PicoCart
lineage below, and avoids relicensing anything belonging to wilco2009 or bwack.

## Licence status

**The upstream repository carries no licence file at all.** The GitHub API
reports `"license": null`, `/license` 404s, and there is no LICENSE, COPYING or
NOTICE anywhere in the tree. By default that means all rights reserved.

**Andrea Ottaviani has granted permission for FujiNet to use his work here**
(reported by Thomas Cherryhomes, 2026-09-02; the grant itself was obtained
directly from the author). That covers aotta's own contributions.

### One gap that remains, and is not aotta's to close

Every C file in PicoPAC carries the header:

> parts of code are directly from the A8PicoCart project by Robin Edwards 2023

[robinhedwards/A8PicoCart](https://github.com/robinhedwards/A8PicoCart) is
**also unlicensed**, and the file set here is a close clone of it — `main.c`,
`msc_disk.c`, `usb_descriptors.c`, `fatfs_disk.c`, `flash_fs.c`, `myboard.h`,
`tusb_config.h`, `fatfs/`, down to the flash-filesystem magic `"RHE!FS30"`
(Robin **H**enry **E**dwards). A permission from aotta does not extend to Robin
Edwards's code, because it was never aotta's to license.

This is the same situation the Intellivision port hit with PiRTO II — see
[[../../intellivision/PROVENANCE.md]] and the note there about A8PicoCart. It was
resolved there by re-vendoring onto Minty, which ships GPLv3. **Someone should
seek the same grant from Robin Edwards before this is published**, or the
A8PicoCart-derived files should be replaced. Flagging it rather than assuming
one permission settles the whole tree.

Third-party components with clear licences of their own, unaffected by any of
the above: **FatFs** (ChaN, BSD-1-Clause) under `fatfs/`, and **TinyUSB** (MIT),
which comes from the pico-sdk rather than from here.

## Hardware

PicoPAC requires a **"purple" RP2040 clone with 16MB flash**, not a genuine
Raspberry Pi Pico: the design needs GP23, GP24 and GP25 as ordinary header
GPIOs, which the official board does not break out. Its own README is emphatic
about this.

It also has **no bus protection whatsoever** — the entire BOM is two pin
sockets, a pushbutton, a 1N4148 and the edge connector. 5 V TTL goes straight
into the RP2040's clamp diodes, and its README warns *"DO NOT CONNECT PICO WHILE
INSERTED IN A POWERED ON CONSOLE!"*. Any board FujiNet ships should level-shift
DB0–DB7 and the address lines properly.

## Local changes to the vendored files

Kept as small and as obvious as possible, so a future upstream merge stays
tractable. Each is marked in place with a `FUJINET:` comment, and the
behavioural ones sit inside `#if CONFIG_FUJINET` — `./build-cart.sh --stock`
still produces a stock PicoPAC, and it is built as a regression check.

**Bug fix, applying regardless of `CONFIG_FUJINET`:**

- `picopac_cart.c`: `extram` was declared `[0xff]` — 255 entries — but indexed
  `[addr & 0xff]`, so the top address read and wrote one past the end. Not
  hypothetical: the menu handshake tests `extram[0xff]` specifically. Now
  `[0x100]`.

**Building against a current pico-sdk:**

- `picopac_cart.c`: `u_int8_t` → `uint8_t` (7 places); the BSD spelling is not
  declared by the SDK's headers.
- `picopac_cart.c`: added `hardware/clocks.h` for `set_sys_clock_khz()`, which
  older SDKs pulled in implicitly.
- `tusb_config.h`: added a `CFG_TUSB_RHPORT0_MODE` fallback. The TinyUSB bundled
  with the SDK here predates `BOARD_TUD_RHPORT` and fails a static assert
  without it.

**FujiNet hooks:**

- `picopac_cart.c`: core1 records writes to `$E0-$E3` into a ring for core0
  (`fuji_cart_note_write`), and swaps the ROM pointer when it sees the console
  fetch the cartridge reset vector with an image staged.
- `picopac_cart.c`: `rom_table` is now reached through a `fuji_rom` pointer, so
  a network-booted image takes over with one atomic store rather than a 32KB
  memcpy racing the running console.
- `picopac_cart.c`: `picopac_cart_main()` loads the baked-in client and runs the
  mailbox service instead of building a menu and loading `selectgame.bin`.
- `CMakeLists.txt`: the `CONFIG_FUJINET` option and the sources under `src/`.
