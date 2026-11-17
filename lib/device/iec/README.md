# lib/device/iec

The Commodore device set for `BUILD_IEC`, on ESP32 and FujiNet-PC.

## How it fits
- Three device bases coexist: `iecFuji`, `iecNetwork` and `iecClock` derive the bus's
  `virtualDevice` (an `IECDevice` underneath) and implement `processCommand()`; `iecDrive`
  derives the library's `IECFileDevice`; the modem and printer derive `IECDevice` directly.
- Devices are registered with `attachDevice()` from [lib/bus/iec/](../../bus/iec/), not
  `addDevice()`. [lib/media/cbm/](../../media/cbm/) supplies
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
- `drive.h` can target a `USE_VDRIVE` backend whose headers are not in this tree.
- `cpm.h`, `cpm.cpp`: `iecCpm` is an empty placeholder.
- `dos/_dos.h` holds commented-out DOS tables and is included by nothing.
