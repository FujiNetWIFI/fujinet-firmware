# pico/common

Code the cartridge firmwares share: the FujiBus SLIP codec, its USB CDC transport to the
ESP32-S3, a CDC-only USB device, a disk track cache for I/O cards, and the start of the carts'
CMake builds.

## How it fits
- Each cart's `build-cart.sh` sets its board, firmware name, baked headers and `cart_gen`, then
  sources `scripts/build-cart.lib.sh`; nes and o2 keep their own.
- The carts' `firmware/CMakeLists.txt` include `cmake/fujicart_pre.cmake` and build with
  `fujicart_firmware()`; nes still writes its build out by hand. [o2/](../o2/) keeps
  PicoPAC's board setup and takes only `cmake/pico_sdk_import.cmake`; intellivision, coco and mac
  have their own.
- [arcadia/](../arcadia/), [astrocade/](../astrocade/), [channelf/](../channelf/),
  [coleco/](../coleco/), [nes/](../nes/), [sms/](../sms/), [studio2/](../studio2/),
  [atari-5200/](../atari-5200/), [atari-7800/](../atari-7800/) and [template/](../template/) list all of it among their `fujicart_firmware()` sources, through
  `FUJI_COMMON`, and each names its `USB_PRODUCT` there. [s100/](../s100/) takes the codec and
  the USB device but has its own link in place of `fujibus_usb.c`.
- [o2/](../o2/) takes only the codec and transport; its USB device stays in `o2/firmware` because
  it adds a mass-storage interface, and its `fujimail.c` is an older design.
- Every cart but o2 builds `src/fujimail.c`, which sits on top of `fujibus_usb.h`.
- Every cart but nes and o2 starts fujimail through `src/fuji_port.c`, and so builds
  `src/fuji_cfg.c` too. nes fills in a `fujimail_port_t` itself, as the MAME devices do with
  `emu/fujitcp.c` as the link.
- arcadia, astrocade, channelf, coleco, nes, studio2, atari-5200, sms, atari-7800 and template set
  their clock in the board header and start with `fuji_clock_init()`. None defines TinyUSB's mount or CDC
  callbacks; the stack's weak defaults do.
- sms, studio2, atari-5200 and atari-7800 take an image's CRC with `src/fuji_crc.c`, and all but
  studio2 look it up in their `*map_db.c`; their `emu/apply.sh` copy it into MAME too. sms
  and atari-7800 generate that table with a `tools/mkmapdb.py` built on `tools/softlist.py`.
- arcadia and channelf build `src/fuji_tick.c` and each define `fuji_tick_target()`; both list
  `test_tick`, which comes from here.
- studio2, atari-5200, coleco, sms and atari-7800 build `src/fuji_watchdog.c`: the first three
  watch power alone, sms and atari-7800 the console's clock too.
- sms and atari-7800 build `src/fuji_store.c`, each with a `fuji_store_cfg.h` of its own.
  astrocade (three tiers), coleco (SRAM only) and nes keep their own store; their
  `include/fuji_store.h` comes first on the include path.
- sms, atari-5200, atari-7800 and coleco also copy `src/fuji_cfg.c` into MAME through `emu/apply.sh`, so
  the device reads a .cfg as the cart does.
- Each cart's `fuji_mailbox.h` but o2's holds its address map and its own registers, and
  includes `fuji_mailbox_proto.h` first and `fuji_mailbox_check.h` last.
- Every MAME cart's `emu/apply.sh` grafts this code into MAME through `emu/graft.lib.sh`, so the
  emulators run the same code; it keeps only its own driver and slot edits. A function added to a
  module goes in `emu/fujins.syms` too. o2's (o2em) and s100's (SIMH) copy what they need by hand.
- [s100/](../s100/) builds `src/fuji_tcache.c` for its floppy controller, with its own
  `include/fuji_tcache_cfg.h` (drives, sector size, slots) and `fuji_tcache_accept()` (which
  disk geometries it takes).
- [intellivision/](../intellivision/) keeps its own copy; its transport has a different API.
- Each cart's `firmware/host_test/Makefile` lists its tests and includes `host_test/cart.mk`; a
  listed test the cart does not have, such as `test_fujimail`, comes from here. The pico
  cartridges workflow runs every one of them.

## Build
Not part of the ESP32 or FujiNet-PC builds. pico-sdk compiles it inside each cart's firmware;
`make -C host_test` runs the codec, .cfg, `fuji_port`, store, watchdog, CRC and track-cache
tests.

## Notes
- `cmake/pico_sdk_import.cmake` is the pico-sdk's own import script, one copy for every cart.
- `src/fujibus.c` is a plain-C client of `lib/bus/FujiBusPacket.cpp`.
- Every link through `fujicart_firmware()` fails unless core1's loop, everything it branches to
  and its named state are in SRAM, with no branch through a register (`tools/checksram.py`).
- `host_test/stub/` is used by astrocade's store test too, and `host_test/check.h` by s100's
  tests.
