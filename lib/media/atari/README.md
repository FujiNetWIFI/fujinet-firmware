# lib/media/atari

Atari disk and cassette image formats for `BUILD_ATARI`.

## Layout
| File | Defines |
|---|---|
| `diskType.h`, `diskType.cpp` | this platform's `MediaType` base: sector `read()`, `write()`, `format()`, `status()`, the PERCOM block, and `discover_mediatype()` |
| `diskTypeAtr.h`, `diskTypeAtr.cpp` | `MediaTypeATR`, plain sector images |
| `diskTypeAtx.h`, `diskTypeAtx.cpp` | `MediaTypeATX`, protected disks with per-sector timing; the `AtxTrack` and `AtxSector` tables live in PSRAM |
| `diskTypeXex.h`, `diskTypeXex.cpp` | `MediaTypeXEX`, an executable presented as a bootable disk |
| `casTape.h` | cassette block and baud timing helpers (`CAS_DEFAULT_BAUD`, `cas_bits_duration_ms()`) used by the cassette device |

## How it fits
- `discover_mediatype()` maps XEX, COM and BIN to `MEDIATYPE_XEX`, and ATR, ATX, CAS and WAV to
  their own types. `sioDisk::mount()` in [lib/device/sio/](../../device/sio/) picks the subclass and
  routes CAS and WAV to the cassette device instead of a disk.
- Included by [lib/media/media.h](../media.h) under `BUILD_ATARI`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_ATARI`. PC: `FUJINET_TARGET=ATARI`
lists every file; `tests/CasTapeTests.cpp` covers `casTape.h`.
