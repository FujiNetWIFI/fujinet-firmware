# pico/common

FujiBus code that the cartridge firmwares share: the SLIP codec, its USB CDC transport to the
ESP32-S3, and a CDC-only USB device.

## Layout
| File | Defines |
|---|---|
| `include/fujibus.h`, `src/fujibus.c` | SLIP + FujiBus codec, a plain-C client of `lib/bus/rs232/FujiBusPacket.cpp`; device and command IDs |
| `include/fujibus_usb.h`, `src/fujibus_usb.c` | one request/reply round trip over USB CDC, and the hook for frames the ESP32 pushes mid-transaction |
| `usb/usb_descriptors.c`, `usb/tusb_config.h` | the CDC-only TinyUSB device; the product string is the cart's `FUJI_USB_PRODUCT` |
| `host_test/test_fujibus.c` | runs the codec self-test on the host |

## How it fits
- [astrocade/](../astrocade/) and [channelf/](../channelf/) build all of it from their
  `firmware/CMakeLists.txt` through `FUJI_COMMON`, and each defines `FUJI_USB_PRODUCT` there.
- [o2/](../o2/) uses `include/` and `src/` only; its USB device stays in `o2/firmware` because it
  adds a mass-storage interface.
- Each cart's own `fujimail.c` sits on top of `fujibus_usb.h`. The `emu/apply.sh` scripts copy
  `fujibus.[ch]` from here into MAME or o2em, so the emulators run the same codec.
- [intellivision/](../intellivision/) keeps its own copy; its transport has a different API.

## Build
Not part of the ESP32 or FujiNet-PC builds. pico-sdk compiles it inside each cart's firmware, and
the pico cartridges workflow runs the codec test:

```sh
cd host_test
gcc -Wall -Wextra -Werror -I../include -o /tmp/test_fujibus test_fujibus.c ../src/fujibus.c && /tmp/test_fujibus
```
