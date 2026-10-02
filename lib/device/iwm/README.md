# lib/device/iwm

The Apple II device set for `BUILD_APPLE`: SmartPort devices plus Disk II emulation.

## Layout
| File | Defines |
|---|---|
| `iwmFuji.h`, `iwmFuji.cpp` | `iwmFuji : fujiDevice` (`.po` images, Apple lobby); defines `platformFuji` and sets `theFuji`; `MAX_SPDISK_DEVICES` SmartPort slots plus `MAX_DISK2_DEVICES` Disk II slots, mapped by `get_disk_dev()` |
| `disk.h`, `disk.cpp` | `iwmDisk`, the `DISK_DEVICE` (SmartPort block device) |
| `disk2.h`, `disk2.cpp` | `iwmDisk2 : iwmDisk`, the Disk II emulation driven by the bus's Disk II service loop |
| `iwmNetwork.h`, `iwmNetwork.cpp` | `iwmNetwork : NDevice` |
| `iwmClock.h`, `iwmClock.cpp` | `iwmClock : fujiClock`; defines `platformClock` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `iwmPrinter` and `printerlist`; defines `fnPrinters` |
| `modem.h`, `modem.cpp` | `iwmModem` |
| `cpm.h`, `cpm.cpp` | `iwmCPM`, CP/M through [lib/runcpm/](../../runcpm/) |

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/iwm/](../../bus/iwm/), implements the
  SmartPort verbs it supports and supplies `create_dib_reply_packet()` and
  `create_status_reply_packet()`. Images come from [lib/media/apple/](../../media/apple/).
- `iwmFuji` overrides `setup()`, `set_additional_direntry_details()`, `get_disk_dev()`,
  `iwm_ctrl()`, `iwm_status()`, `fujicmd_close_directory()` and `fujicmd_read_directory_entry()`;
  `setup()` registers itself, the networks, `platformClock`, CP/M and the disks. `iwmClock` maps
  writes to `iwm_ctrl()` and reads to `iwm_status()` and overrides `fujidev_canonical_command()` and
  `reject_command()`.
- `src/main.cpp` adds the modem and printer before calling `theFuji->setup()`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_APPLE`. PC: `FUJINET_TARGET=APPLE`
lists every file, but `disk2.cpp` is wrapped in `ESP_PLATFORM`, so Disk II exists only on hardware.
