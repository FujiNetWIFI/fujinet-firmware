# lib/device/rs232

The device set for `BUILD_RS232`, shared by the PC/MS-DOS RS-232 boards and every `fujiversal-*`
cartridge board.

## Layout
| File | Defines |
|---|---|
| `rs232Fuji.h`, `rs232Fuji.cpp` | `rs232Fuji : fujiDevice` (`.img` images, MS-DOS lobby); defines `platformFuji` and sets `theFuji` |
| `disk.h`, `disk.cpp` | `rs232Disk`, the `DISK_DEVICE`; `mount()` takes the owning `fujiHost` so `MediaTypeROM` can open a memory-map sibling file |
| `rs232Network.h`, `rs232Network.cpp` | `rs232Network : NDevice` |
| `rs232Clock.h`, `rs232Clock.cpp` | `rs232Clock : fujiClock`; defines `platformClock` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `rs232Printer` and `printerlist`; defines `fnPrinters` |
| `modem.h`, `modem.cpp` | `rs232Modem` |
| `rs232cpm.h`, `rs232cpm.cpp` | `rs232CPM`, CP/M through [lib/runcpm/](../../runcpm/) |

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/rs232/](../../bus/rs232/) and implements
  `rs232_process()`. Images come from [lib/media/rs232/](../../media/rs232/).
- `rs232Fuji` overrides `setup()`, `set_additional_direntry_details()`, `mount_media()` (to pass the
  host to the disk) and `appkey_read()`. `rs232Network` overrides `fujidev_set_query()`.
  `rs232Clock` overrides `fujidev_read_tz()` and `fujidev_alt_requested()`.
- `src/main.cpp` adds the Fuji, clock, printer and modem devices.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_RS232`. PC: `FUJINET_TARGET=RS232`
lists every file.
