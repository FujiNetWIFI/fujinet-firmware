# pico/common

Code the cartridge firmwares share: the FujiBus SLIP codec, its USB CDC transport to the
ESP32-S3, a CDC-only USB device, and the start of the carts' CMake builds.

## Layout
| File | Defines |
|---|---|
| `cmake/fujicart_pre.cmake` | what a cart's `CMakeLists.txt` runs before `project()`: board selection from `FUJI_DEFAULT_BOARD` or `-DPICO_BOARD`, the `FUJI_GEN` default, the SDK import |
| `cmake/pico_sdk_import.cmake` | the pico-sdk's import script, one copy for every cart |
| `include/fujibus.h`, `src/fujibus.c` | SLIP + FujiBus codec, a plain-C client of `lib/bus/rs232/FujiBusPacket.cpp`; device and command IDs |
| `include/fujibus_usb.h`, `src/fujibus_usb.c` | one request/reply round trip over USB CDC, and the hook for frames the ESP32 pushes mid-transaction |
| `include/fujimail.h`, `src/fujimail.c` | the mailbox service: hotspot and register decode, the SEQ/ACKSEQ interlock, the DBC push receiver; the cart or MAME device supplies a `fujimail_port_t` and its own `fuji_mailbox.h` |
| `include/fuji_mailbox_proto.h` | the mailbox protocol: register numbers 0x00-0x13, magics, status and error values, `FN_TX_MAX` |
| `include/fuji_mailbox_check.h` | `#error`s when a cart's `fuji_mailbox.h` moves a status field or hotspot page from where `fujimail.c` expects it |
| `emu/fujitcp.c`, `emu/fujitcp.h` | the MAME grafts' FujiBus link to fujinet-pc over TCP, in place of `fujibus_usb.c` |
| `usb/usb_descriptors.c`, `usb/tusb_config.h` | the CDC-only TinyUSB device; the product string is the cart's `FUJI_USB_PRODUCT` |
| `host_test/test_fujibus.c` | runs the codec self-test on the host |
| `host_test/cart.mk` | the rules every cart's `host_test/Makefile` includes: one compile rule, the runner, binaries in the cart's `build/host_test`, and a C++ compile of the tested sources, which the MAME grafts build as C++ |

## How it fits
- The carts' `firmware/CMakeLists.txt` include `cmake/fujicart_pre.cmake`. [o2/](../o2/) keeps
  PicoPAC's board setup and takes only `cmake/pico_sdk_import.cmake`; intellivision, coco and mac
  have their own.
- [arcadia/](../arcadia/), [astrocade/](../astrocade/), [channelf/](../channelf/),
  [nes/](../nes/), [sms/](../sms/), [studio2/](../studio2/), [atari-5200/](../atari-5200/) and
  [atari-7800/](../atari-7800/) build all of it from their `firmware/CMakeLists.txt` through
  `FUJI_COMMON`, and each defines `FUJI_USB_PRODUCT` there. [s100/](../s100/) takes the codec and
  the USB device but has its own link in place of `fujibus_usb.c`.
- [o2/](../o2/) takes only the codec and transport; its USB device stays in `o2/firmware` because
  it adds a mass-storage interface, and its `fujimail.c` is an older design.
- Every cart but o2 builds `src/fujimail.c`, which sits on top of `fujibus_usb.h`.
- Each cart's `fuji_mailbox.h` but o2's holds its address map and its own registers, and
  includes `fuji_mailbox_proto.h` first and `fuji_mailbox_check.h` last.
- The `emu/apply.sh` scripts copy `fujibus.[ch]`, `fujimail.[ch]`, the two mailbox headers and
  `emu/fujitcp.[ch]` from here into MAME (o2's takes only the codec, into o2em), so the emulators
  run the same code.
- [intellivision/](../intellivision/) keeps its own copy; its transport has a different API.
- Each cart's `firmware/host_test/Makefile` lists its tests and includes `host_test/cart.mk`; the
  pico cartridges workflow runs every one of them.

## Build
Not part of the ESP32 or FujiNet-PC builds. pico-sdk compiles it inside each cart's firmware;
`make -C host_test` runs the codec test.
