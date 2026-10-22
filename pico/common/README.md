# pico/common

Code the cartridge firmwares share: the FujiBus SLIP codec, its USB CDC transport to the
ESP32-S3, a CDC-only USB device, and the start of the carts' CMake builds.

## Layout
| File | Defines |
|---|---|
| `cmake/fujicart_pre.cmake` | what a cart's `CMakeLists.txt` runs before `project()`: board selection from `FUJI_DEFAULT_BOARD` or `-DPICO_BOARD`, the `FUJI_GEN` default, `FUJI_COMMON`, the SDK import |
| `cmake/fujicart.cmake` | `fujicart_firmware()`: the executable from the cart's sources in link order, `-Os -Wall`, `FUJI_USB_PRODUCT`, the include path (`build/gen` before `baked/`), the SDK libraries, `copy_to_ram`, PIO headers, stdio off, and the `CORE1` SRAM check |
| `cmake/pico_sdk_import.cmake` | the pico-sdk's import script, one copy for every cart |
| `cmake/fujicart_checksram.cmake`, `tools/checksram.py` | `fujicart_checksram()`, run after every link for `fujicart_firmware()`'s `CORE1`: core1's loop, everything it branches to and the named state must be in SRAM, with no branch through a register |
| `tools/mkromh.py` | packs the client a cart serves at power-on into `fujiconfigrom.h`, after the size, claim and header checks the cart passes it |
| `tools/softlist.py` | what a cart's `tools/mkmapdb.py` reads from a MAME software list: each cart's slot, cart RAM and whole-image CRC-32, its chips joined |
| `scripts/build-cart.lib.sh` | the shared body of every cart's `build-cart.sh`: the default, `--baked` and `--bake` modes, the build into `build/fw-<board>` |
| `include/fujibus.h`, `src/fujibus.c` | SLIP + FujiBus codec, a plain-C client of `lib/bus/FujiBusPacket.cpp`; device and command IDs |
| `include/fujibus_usb.h`, `src/fujibus_usb.c` | one request/reply round trip over USB CDC, and the hook for frames the ESP32 pushes mid-transaction |
| `include/fujimail.h`, `src/fujimail.c` | the mailbox service: hotspot and register decode, the SEQ/ACKSEQ interlock, the DBC push receiver; the cart or MAME device supplies a `fujimail_port_t` and its own `fuji_mailbox.h`; registers from `FN_REG_PLATFORM` up go to the port's `own_reg` |
| `include/fuji_mailbox_proto.h` | the mailbox protocol: register numbers 0x00-0x13, magics, status and error values, `FN_TX_MAX` |
| `include/fuji_mailbox_check.h` | `#error`s when a cart's `fuji_mailbox.h` moves a status field or hotspot page from where `fujimail.c` expects it |
| `include/fuji_cfg.h`, `src/fuji_cfg.c` | the .cfg sibling a push sends before an image: a line starting `mapper=` names the next image's mapper |
| `include/fuji_port.h`, `src/fuji_port.c` | fujimail on a cartridge: binds it to `fujibus_usb.c`, BOOTSEL and the .cfg sibling, leaving the cart a `fuji_port_ops_t` of its own hooks |
| `include/fuji_store.h`, `src/fuji_store.c` | where a pushed image waits for the console: a RAM tier, and a flash tier kept blank ahead of time so erases stay out of the push; the cart's `fuji_store_cfg.h` sizes both |
| `include/fuji_crc.h`, `src/fuji_crc.c` | CRC-32 (zlib's), and the lookup in a cart's table of known images, rows keyed by it and sorted |
| `include/fuji_clock.h` | `fuji_clock_init()`: the system clock the board header sets in `FUJI_SYS_CLOCK_KHZ`, at its `FUJI_VREG_VOLTAGE` if it sets one |
| `include/fuji_watchdog.h`, `src/fuji_watchdog.c` | notices the console going away: PWR_OK low, or its clock (counted on a PWM slice) stopped, for longer than the cart's timeout; the cart decides what that means |
| `emu/fujitcp.c`, `emu/fujitcp.h` | the MAME grafts' FujiBus link to fujinet-pc over TCP, in place of `fujibus_usb.c` |
| `usb/usb_descriptors.c`, `usb/tusb_config.h` | the CDC-only TinyUSB device; the product string is the cart's `FUJI_USB_PRODUCT` |
| `host_test/test_fujibus.c` | runs the codec self-test on the host |
| `host_test/test_fuji_cfg.c` | the `mapper=` parser's tests |
| `host_test/test_fuji_port.c` | what `fuji_port.c` hands fujimail, against stand-ins for the USB link |
| `host_test/test_fuji_store.c`, `host_test/fuji_store_cfg.h` | the push store, under ASan, on a small store of its own |
| `host_test/test_fuji_crc.c` | the CRC against zlib's check values, and the table lookup |
| `host_test/test_fuji_watchdog.c` | the watchdog, with power, clock and time in the test's hands |
| `host_test/stub/` | stand-ins for the SDK headers these tests need; `hardware/flash.h` models NOR flash, where a program only clears bits. astrocade's store test uses them too |
| `host_test/test_fujimail.c` | the mailbox service's tests, which every cart runs against its own `fuji_mailbox.h`; the cart's `host_test/fujimail_test.h` names its mapper's gate and image sizes |
| `host_test/cart.mk` | the rules every cart's `host_test/Makefile` includes: one compile rule, the runner, binaries in the cart's `build/host_test`, and a C++ compile of the tested sources, which the MAME grafts build as C++ |

## How it fits
- Each cart's `build-cart.sh` sets its board, firmware name, baked headers and `cart_gen`, then
  sources `scripts/build-cart.lib.sh`; nes and o2 keep their own.
- The carts' `firmware/CMakeLists.txt` include `cmake/fujicart_pre.cmake` and build with
  `fujicart_firmware()`; nes still writes its build out by hand. [o2/](../o2/) keeps
  PicoPAC's board setup and takes only `cmake/pico_sdk_import.cmake`; intellivision, coco and mac
  have their own.
- [arcadia/](../arcadia/), [astrocade/](../astrocade/), [channelf/](../channelf/),
  [nes/](../nes/), [sms/](../sms/), [studio2/](../studio2/), [atari-5200/](../atari-5200/) and
  [atari-7800/](../atari-7800/) list all of it among their `fujicart_firmware()` sources, through
  `FUJI_COMMON`, and each names its `USB_PRODUCT` there. [s100/](../s100/) takes the codec and
  the USB device but has its own link in place of `fujibus_usb.c`.
- [o2/](../o2/) takes only the codec and transport; its USB device stays in `o2/firmware` because
  it adds a mass-storage interface, and its `fujimail.c` is an older design.
- Every cart but o2 builds `src/fujimail.c`, which sits on top of `fujibus_usb.h`.
- Every cart but nes and o2 starts fujimail through `src/fuji_port.c`, and so builds
  `src/fuji_cfg.c` too. nes fills in a `fujimail_port_t` itself, as the MAME devices do with
  `emu/fujitcp.c` as the link.
- arcadia, astrocade, channelf, studio2, atari-5200, sms and atari-7800 set their clock in the
  board header and start with `fuji_clock_init()`. None defines TinyUSB's mount or CDC
  callbacks; the stack's weak defaults do.
- sms, studio2, atari-5200 and atari-7800 take an image's CRC with `src/fuji_crc.c`, and all but
  studio2 look it up in their `*map_db.c`; their `emu/apply.sh` copy it into MAME too. sms
  and atari-7800 generate that table with a `tools/mkmapdb.py` built on `tools/softlist.py`.
- studio2, atari-5200, sms and atari-7800 build `src/fuji_watchdog.c`: the first two watch power
  alone, sms and atari-7800 the console's clock too.
- sms and atari-7800 build `src/fuji_store.c`, each with a `fuji_store_cfg.h` of its own.
  astrocade (three tiers) and nes keep their own store; their `include/fuji_store.h` comes first
  on the include path.
- sms, atari-5200 and atari-7800 also copy `src/fuji_cfg.c` into MAME through `emu/apply.sh`, so
  the device reads a .cfg as the cart does.
- Each cart's `fuji_mailbox.h` but o2's holds its address map and its own registers, and
  includes `fuji_mailbox_proto.h` first and `fuji_mailbox_check.h` last.
- The `emu/apply.sh` scripts copy `fujibus.[ch]`, `fujimail.[ch]`, the two mailbox headers and
  `emu/fujitcp.[ch]` from here into MAME (o2's takes only the codec, into o2em), so the emulators
  run the same code.
- [intellivision/](../intellivision/) keeps its own copy; its transport has a different API.
- Each cart's `firmware/host_test/Makefile` lists its tests and includes `host_test/cart.mk`; a
  listed test the cart does not have, such as `test_fujimail`, comes from here. The pico
  cartridges workflow runs every one of them.

## Build
Not part of the ESP32 or FujiNet-PC builds. pico-sdk compiles it inside each cart's firmware;
`make -C host_test` runs the codec, .cfg, `fuji_port`, store, watchdog and CRC tests.
