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
| `casFSK.h`, `casFSK.cpp` | A8CAS `fsk ` run rules: chunk checks and the join rule, `fsk_scan_run()`, value timing and the value-to-symbol encoder helpers |
| `casFSKLoader.h`, `casFSKLoader.cpp` | the progressive loader of one `fsk ` run: producer steps, published values, the consumer's value source and the start gate |

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
