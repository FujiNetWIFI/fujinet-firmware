# lib/device/iec

The Commodore device set for `BUILD_IEC`, on ESP32 and FujiNet-PC.

## Layout
| File | Defines |
|---|---|
| `iecFuji.h`, `iecFuji.cpp` | `iecFuji : fujiDevice` (`.d64` images); defines `platformFuji` and sets `theFuji`; `setup()` attaches the printer, the drives, the network, the clock and itself to the bus under their Commodore device numbers |
| `drive.h`, `drive.cpp` | `iecDrive : IECFileDevice`, the `DISK_DEVICE`: a Commodore drive whose channels (`iecChannelHandler`), directory and disk images come from [lib/meatloaf/](../../meatloaf/); `write_blank()` writes the image for NEW DISK |
| `iecNetwork.h`, `iecNetwork.cpp` | `iecNetwork : NDevice` |
| `iecClock.h`, `iecClock.cpp` | `iecClock : IECDevice`, a raw IEC clock device |
| `modem.h`, `modem.cpp` | `iecModem : IECDevice` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `iecPrinter : IECDevice` and `printerlist`; defines `fnPrinters` |
| `cpm.h`, `cpm.cpp` | `iecCpm`, an empty placeholder |
| `dos/_dos.h` | commented-out DOS tables, included by nothing |

## How it fits
- Three device bases coexist: `iecFuji` and `iecNetwork` derive the bus's `virtualDevice` (an
  `IECDevice` underneath) and implement `processCommand()`; `iecDrive` derives the library's
  `IECFileDevice`; the clock, modem and printer derive `IECDevice` directly and do not use
  `fujiClock`.
- `iecFuji` overrides `setup()`, `set_additional_direntry_details()` and `processCommand()`.
  `iecNetwork` overrides `fujidev_write()`. Devices are registered with `attachDevice()` from
  [lib/bus/iec/](../../bus/iec/), not `addDevice()`. [lib/media/cbm/](../../media/cbm/) supplies
  the `mediatype_t` values and the blank images NEW DISK writes; image handling is in Meatloaf.
- NEW DISK is the shared `fujiDevice::fujicmd_new_disk()`; `iecFuji` has no override.
  `iecDrive::NewDiskRequest` is the layout fujinet-lib sends from a C64 (`numSectors`, `sectorSize`,
  host slot, device slot, a 256-byte name), and `write_blank()` passes the file to
  `BlankImage::create()`: a formatted image for a `.d64`, `.d71` or `.d81` name, zeros for any other.
- A drive reads a mounted image through the handle the Fuji device opened: `mount()` attaches it
  to Meatloaf's `fnfile://` file system, and `unmount()` detaches and closes it. With no image
  mounted, a drive sees the flash directory as `/`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_IEC`. PC: `FUJINET_TARGET=IEC`.

## Notes
`drive.h` can target a `USE_VDRIVE` backend whose headers are not in this tree.
