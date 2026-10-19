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
| `usb/usb_descriptors.c`, `usb/tusb_config.h` | the CDC-only TinyUSB device; the product string is the cart's `FUJI_USB_PRODUCT` |
| `host_test/test_fujibus.c` | runs the codec self-test on the host |
| `host_test/cart.mk` | the rules every cart's `host_test/Makefile` includes: one compile rule, the runner, binaries in the cart's `build/host_test` |

## How it fits
- The carts' `firmware/CMakeLists.txt` include `cmake/fujicart_pre.cmake`. [o2/](../o2/) keeps
  PicoPAC's board setup and takes only `cmake/pico_sdk_import.cmake`; intellivision, coco and mac
  have their own.
- [astrocade/](../astrocade/), [channelf/](../channelf/) and [studio2/](../studio2/) build all
  of it from their `firmware/CMakeLists.txt` through `FUJI_COMMON`, and each defines
  `FUJI_USB_PRODUCT` there.
- [o2/](../o2/) uses `include/` and `src/` only; its USB device stays in `o2/firmware` because it
  adds a mass-storage interface.
- Each cart's own `fujimail.c` sits on top of `fujibus_usb.h`. The `emu/apply.sh` scripts copy
  `fujibus.[ch]` from here into MAME or o2em, so the emulators run the same codec.
- [intellivision/](../intellivision/) keeps its own copy; its transport has a different API.
- Each cart's `firmware/host_test/Makefile` lists its tests and includes `host_test/cart.mk`; the
  pico cartridges workflow runs every one of them.

## Build
Not part of the ESP32 or FujiNet-PC builds. pico-sdk compiles it inside each cart's firmware;
`make -C host_test` runs the codec test.
