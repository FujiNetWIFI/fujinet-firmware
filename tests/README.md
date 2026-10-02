# tests

Host-side unit tests and policy checks for the FujiNet-PC build. ctest runs them at the end of
`./build.sh -p <TARGET>` and in the Build FujiNet-PC workflow.

## Layout
| File | Covers |
|---|---|
| `CMakeLists.txt` | One `add_executable` and `add_test` pair per test; each links only the unit under test, never the `fujinet` executable |
| `FujiBusPacketTests.cpp` | `lib/bus/rs232/FujiBusPacket.cpp` SLIP framing (`fujibuspacket_tests`) |
| `FnSanitizeTests.cpp` | `lib/fntext/fn_sanitize.cpp` entity decoding and UTF-8 folding (`fn_sanitize_tests`) |
| `FnxmlQueryTests.cpp` | `lib/fnxml/fnxml_query.cpp` XPath subset over tinyxml2 (`fnxml_query_tests`) |
| `CalendarTests.cpp` | `lib/utils/fn_time.cpp` and `lib/network-protocol/calendar_draft.cpp` (`calendar_tests`) |
| `MailDraftTests.cpp` | `lib/network-protocol/mail_draft.cpp` RFC822 compose and reply (`mail_tests`) |
| `IMDImageTests.cpp`, `IMDFixture.h` | `lib/media/IMDImage.cpp` (`imdimage_tests`; built with `UNIT_TESTS` and `FNIO_IS_STDIO`) |
| `MediaTypeIMDTests.cpp` | `lib/media/rs232/diskTypeIMD.cpp` adapter (`mediatypeimd_tests`; built with `BUILD_RS232`) |
| `sioNetwork-DSTATS-test.cpp` | `sioNetwork` command recognition and DSTATS (`sio_dstats_tests`; ATARI target only) |
| `NQueryOutputModeTests.cpp`, `NParserReadTests.cpp` | `NParser` and `XMLParser` query output modes and short reads (`nquery_output_mode_tests`; ATARI target only) |
| `HttpHeaderTests.cpp` | `lib/fn_esp_http_client/fn_http_header.cpp` header chunking (`http_header_tests`; skipped where the BSD queue header is missing) |
| `mac_gcr_test.cpp` | `lib/media/mac/macGCR.cpp` encode and decode round trip (`mac_gcr_tests`) |
| `CasTapeTests.cpp`, `CassetteRewindRequestTests.cpp`, `CassetteTrailTests.cpp` | Atari cassette record timing, rewind request and trail (`cassette_tests`) |
| `check_no_build_ifdefs.py` | Policy: no `BUILD_*` macro in a preprocessor conditional under `lib/device/fujiDevice`, `lib/device/fujiClock` or `lib/device/NDevice` (`no_build_ifdefs_in_fujidevice`) |
| `check_no_system_bus.py` | Policy: `SYSTEM_BUS` must not appear in any source under `lib/media` (`no_system_bus_in_media`) |
| `unsit.c`, `stuffit_test.sh` | Host CLI over `lib/stuffit` and a comparison against `unar` (`stuffit_sample_tests`; registered only with `SIT_SAMPLES_DIR`) |
| `ndif_blocks_test.c`, `ndif_test.sh` | NDIF decoding compared with ndif2raw (`ndif_sample_tests`; registered only with `NDIF2RAW_DIR`) |
| `esp_stubs/` | No-op `esp_stubs/esp_err.h` and `esp_stubs/esp_log.h` so `http_header_tests` can compile ESP-only code on a host |
| `corpus_manifest.txt` | Provenance of the StuffIt sample archives; data only, read by nothing |

## How it fits
- Added by the root `CMakeLists.txt` after `fujinet_pc.cmake`, only when `FUJINET_TARGET` is set.
- doctest comes from [components_pc/doctest/](../components_pc/doctest/); the test file that owns
  `main()` defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.

## Build
PC only. `make atari-lwm` (or `./build.sh -p ATARI`) configures, builds and runs `ctest -V`; a
failing test aborts the whole invocation. The firmware workflows compile and never run tests.

## Notes
To add a test, add an executable and an `add_test` to `CMakeLists.txt`, and keep the unit under
test free of hardware and FujiNet globals so it links alone. The PlatformIO directory
[test/](../test/) is not this suite.
