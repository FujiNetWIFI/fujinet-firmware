# lib/media/rom

Cartridge ROM images, which go to the cartridge whole at mount time instead of being read a sector
at a time like a disk.

## Layout

| File | Defines |
|---|---|
| `ROMMedia.h`, `ROMMedia.cpp` | `ROMMedia`, the base for every cartridge format: `size()`, `read()` of the whole file in chunks, and `readSize()` for a format's `attach()` |
| `ROMFactory.h` | `ROMFactory::open<Formats...>()`, which returns the first of the listed formats that accepts the file, or `nullptr`; includes every format's header, so a device includes only this one |
| `CoCoCartROM.h`, `CoCoCartROM.cpp` | `CoCoCartROM`, a CoCo cartridge: `.ccc` or `.rom`, 8K to 128K in a power-of-two number of 8K banks |

## How it fits

- `ROMMedia` derives `Media` from [lib/media/media.h](../media.h); a format is opened through
  `Media::open()`, which runs its `attach()`.
- A device lists the formats it accepts, in order, in a `ROMFactory::open<>()` call and sends what
  it gets back to the cartridge: `drivewireDisk` in
  [lib/device/drivewire/](../../device/drivewire/) opens `CoCoCartROM`.

## Build

ESP: globbed by `src/CMakeLists.txt` into every target. PC: listed in `fujinet_pc.cmake` for every
target.
