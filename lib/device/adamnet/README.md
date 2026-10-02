# lib/device/adamnet

The Coleco ADAM device set for `BUILD_ADAM`.

## Layout
| File | Defines |
|---|---|
| `adamFuji.h`, `adamFuji.cpp` | `adamFuji : fujiDevice` (`.ddp` images); defines `platformFuji` and sets `theFuji`; `setup()` registers four disks, two networks and itself, and mounts `/autorun.ddp` or `/mount-and-boot.ddp` from flash into the first slot |
| `disk.h`, `disk.cpp` | `adamDisk`, the `DISK_DEVICE` |
| `adamNetwork.h`, `adamNetwork.cpp` | `adamNetwork : NDevice` |
| `adamClock.h`, `adamClock.cpp` | `adamClock : fujiClock`; defines `platformClock` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `adamPrinter` (Coleco printer emulation) and `printerlist`; defines `fnPrinters` |
| `keyboard.h`, `keyboard.cpp` | `adamKeyboard`, a virtual keyboard used only behind `VIRTUAL_ADAM_DEVICES` |
| `serial.h`, `serial.cpp` | `adamSerial` |

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/adamnet/](../../bus/adamnet/) and implements
  `adamnet_control_send()` and `deviceStatus()`. Images come from
  [lib/media/adam/](../../media/adam/).
- `adamFuji` overrides `setup()`, `shutdown()`, `set_additional_direntry_details()`,
  `fujidev_set_device_fullpath()` and `fujicmd_read_directory_entry()`. `adamNetwork` overrides
  `adamnet_control_receive()` and `fujidev_write()`. `adamClock` overrides `fujidev_read_tz()`.
- `src/main.cpp` adds the printer (when enabled in config) and `platformClock`; there is no modem
  device on this platform.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_ADAM`. PC: `FUJINET_TARGET=ADAM`
lists every file.
