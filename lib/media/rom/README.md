# lib/media/rom

Cartridge ROM images, which go to the cartridge whole at mount time instead of being read a sector
at a time like a disk.

## Layout

| File | Defines |
|---|---|
| `ROMMedia.h`, `ROMMedia.cpp` | `ROMMedia`, the base for every cartridge format |
| `ROMFactory.h` | `ROMFactory`, which returns the first of a device's listed formats that accepts the file |
| The other `*ROM.h` and `*ROM.cpp` files | One class per ROM image format, such as `CoCoCartROM` or `IntellicartROM`, that handles the details of that format; a platform can have more than one |

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
