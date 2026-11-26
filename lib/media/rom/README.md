# lib/media/rom

Cartridge ROM images, which go to the cartridge whole at mount time instead of being read a sector
at a time like a disk.

## How it fits

- `ROMMedia` derives `Media` from [lib/media/media.h](../media.h); a format is opened through
  `Media::open()`, which runs its `attach()`.
- A platform lists the formats it accepts, in order, in a `MediaFormats` list that its Fuji's
  `openROM()` opens from, as in [lib/device/drivewire/](../../device/drivewire/) and
  [lib/device/rs232/](../../device/rs232/). `Cartridge` in
  [lib/device/Cartridge/](../../device/Cartridge/) pushes what comes back.

## Build

ESP: globbed by `src/CMakeLists.txt` into every target. PC: listed in `fujinet_pc.cmake` for every
target.
