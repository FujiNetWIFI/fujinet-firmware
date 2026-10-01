# lib/media/adam

Coleco ADAM tape and disk image formats for `BUILD_ADAM`, also handed to `BUILD_S100`.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base: block `read()` and `write()`, `status()`, `discover_mediatype()` |
| `mediaTypeDDP.h`, `mediaTypeDDP.cpp` | `MediaTypeDDP`, digital data pack (tape) images |
| `mediaTypeDSK.h`, `mediaTypeDSK.cpp` | `MediaTypeDSK`, disk images |
| `mediaTypeROM.h`, `mediaTypeROM.cpp` | `MediaTypeROM`, cartridge images |

## How it fits
- `discover_mediatype()` maps DDP, DSK and ROM by extension; `adamDisk` in
  [lib/device/adamnet/](../../device/adamnet/) picks the subclass.
- Included by [lib/media/media.h](../media.h) under `BUILD_ADAM`, and again (base and DSK only)
  under `BUILD_S100`, so [lib/media/s100spi/](../s100spi/) is never reached.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_ADAM`. PC: `FUJINET_TARGET=ADAM`
lists every file.
