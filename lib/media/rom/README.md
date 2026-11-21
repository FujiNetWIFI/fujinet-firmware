# lib/media/rom

Cartridge ROM images, which go to the cartridge whole at mount time instead of being read a sector
at a time like a disk. The exception is `PMD85ROM`: the PMD 85 reads its module a page at a time, so
the device keeps it open.

## How it fits

- `ROMMedia` derives `Media` from [lib/media/media.h](../media.h); a format is opened through
  `Media::open()`, which runs its `attach()`.
- A device lists the formats it accepts, in order, in a `ROMFactory::open<>()` call and sends what
  it gets back to the cartridge, as `drivewireDisk` in
  [lib/device/drivewire/](../../device/drivewire/) and `rs232Disk` in
  [lib/device/rs232/](../../device/rs232/) do.

## Build

ESP: globbed by `src/CMakeLists.txt` into every target. PC: listed in `fujinet_pc.cmake` for every
target.
