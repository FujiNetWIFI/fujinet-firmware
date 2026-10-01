# lib/device/drivewire

The CoCo and Dragon device set for `BUILD_COCO`.

## Layout
| File | Defines |
|---|---|
| `drivewireFuji.h`, `drivewireFuji.cpp` | `drivewireFuji : fujiDevice` (`.dsk` images, `MAX_DWDISK_DEVICES` slots, CoCo lobby); defines `platformFuji` and sets `theFuji` |
| `disk.h`, `disk.cpp` | `drivewireDisk : drivewireDevice`, the `DISK_DEVICE` |
| `drivewireNetwork.h`, `drivewireNetwork.cpp` | `drivewireNetwork : NDevice`; the bus creates one per unit on first use |
| `drivewireClock.h`, `drivewireClock.cpp` | `drivewireClock : fujiClock`; defines `platformClock` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `drivewirePrinter : drivewireDevice` and `printerlist`; defines `fnPrinters` |
| `modem.h`, `modem.cpp` | `drivewireModem : drivewireDevice` |
| `cassette.h`, `cassette.cpp` | `drivewireCassette : drivewireDevice` |
| `cpm.h`, `cpm.cpp` | `drivewireCPM`, CP/M through [lib/runcpm/](../../runcpm/) |

## How it fits
- The bus in [lib/bus/drivewire/](../../bus/drivewire/) dispatches by opcode, so the disk, printer,
  modem and cassette derive its `drivewireDevice` base directly; the Fuji, network, clock and CP/M
  devices derive `virtualDevice` and answer `processCommand()`. Images come from
  [lib/media/drivewire/](../../media/drivewire/).
- `drivewireFuji` overrides `setup()`, `shutdown()`, `set_additional_direntry_details()`,
  `processCommand()`, `insert_boot_device()`, `fujicore_mount_disk_image_success()`,
  `fujidev_copy_file()`, `appkey_read()` and `appkey_write()`. `drivewireNetwork` overrides
  `fujidev_set_parser()`. `drivewireClock` overrides `fujidev_read_tz()`.
- `src/main.cpp` hands the printer to the bus with `setPrinter()`; the bus keeps no daisy chain.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_COCO`. PC: `FUJINET_TARGET=COCO`
lists the Fuji, disk, printer, clock and network files; the modem, cassette and CP/M devices are
ESP only.
