# lib/device/rs232

The device set for `BUILD_RS232`, shared by the PC/MS-DOS RS-232 boards and every `fujiversal-*`
cartridge board.

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/rs232/](../../bus/rs232/) and implements
  `rs232_process()`. Images come from [lib/media/rs232/](../../media/rs232/).
- `rs232Fuji` overrides `mount_media()` to pass the host to the disk.
- [lib/device/device.h](../device.h) makes `PlatformFuji` an `h89Fuji` under `PINMAP_H89` and an
  `rs232Fuji` otherwise.
- `src/main.cpp` adds the Fuji, clock, printer and modem devices.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_RS232`. PC: `FUJINET_TARGET=RS232`
lists every file.

## Notes
- `h89Fuji` mounts no boot disk, since the MS-DOS one means nothing to CP/M or HDOS.
- `rs232Disk::mount()` takes the owning `fujiHost` so a ROM format can open a mapper sibling file.
