# lib/media/atari

Atari disk and cassette image formats for `BUILD_ATARI`.

## How it fits
- `discover_mediatype()` maps XEX, COM and BIN to `MEDIATYPE_XEX`, and ATR, ATX, CAS and WAV to
  their own types. `sioDisk::mount()` in [lib/device/sio/](../../device/sio/) picks the subclass and
  routes CAS and WAV to the cassette device instead of a disk.
- Included by [lib/media/media.h](../media.h) under `BUILD_ATARI`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_ATARI`, except `casFSK.cpp` and
`casFSKLoader.cpp`, which have no guard. PC: `FUJINET_TARGET=ATARI`
lists every file; `tests/CasTapeTests.cpp` covers `casTape.h`, `tests/CasFSKTests.cpp` and
`tests/CasFSKLoaderTests.cpp` cover `casFSK` and `casFSKLoader`.
