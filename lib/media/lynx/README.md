# lib/media/lynx

Atari Lynx cartridge images for `BUILD_LYNX`.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base and `discover_mediatype()` |
| `mediaTypeROM.h`, `mediaTypeROM.cpp` | `MediaTypeROM`, the only image type |

## How it fits
- `discover_mediatype()` returns `MEDIATYPE_ROM` for every file; the extension check is commented
  out. `lynxDisk` in [lib/device/comlynx/](../../device/comlynx/) serves the blocks, LZ4-compressed,
  over ComLynx.
- Included by [lib/media/media.h](../media.h) under `BUILD_LYNX`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_LYNX`. PC: `FUJINET_TARGET=LYNX`
lists both pairs.

## Notes
The `mediatype_t` enum still carries `MEDIATYPE_DDP` and `MEDIATYPE_DSK` values copied from the
ADAM media; nothing uses them.
