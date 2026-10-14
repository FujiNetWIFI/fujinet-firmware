# tests

Host-side unit tests and policy checks for the FujiNet-PC build. ctest runs them at the end of
`./build.sh -p <TARGET>` and in the Build FujiNet-PC workflow.

## Layout
| File | Covers |
|---|---|
| `CMakeLists.txt` | One `add_executable` and `add_test` pair per test; each links only the unit under test, never the `fujinet` executable |
| `FujiBusPacketTests.cpp` | `lib/bus/rs232/FujiBusPacket.cpp` SLIP framing (`fujibuspacket_tests`) |
| `DaisyChainTests.cpp` | `lib/bus/DaisyChain.cpp` device-ID rotation behind the disk swap (`daisychain_tests`) |
| `FnSanitizeTests.cpp` | `lib/fntext/fn_sanitize.cpp` entity decoding and UTF-8 folding (`fn_sanitize_tests`) |
| `PeoplesUrlParserTests.cpp` | `lib/utils/peoples_url_parser.cpp` query and fragment splitting, and `keepWholePath()` (`url_parser_tests`) |
| `FnxmlQueryTests.cpp` | `lib/fnxml/fnxml_query.cpp` XPath subset over tinyxml2 (`fnxml_query_tests`) |
| `CalendarTests.cpp` | `lib/utils/fn_time.cpp` and `lib/network-protocol/calendar_draft.cpp` (`calendar_tests`) |
| `QRCodeTests.cpp` | `lib/qrcode/qrcode.c` version and ECC selection (`qrcode_tests`) |
| `MailDraftTests.cpp` | `lib/network-protocol/mail_draft.cpp` RFC822 compose and reply (`mail_tests`) |
| `IMDImageTests.cpp`, `IMDFixture.h` | `lib/media/IMDImage.cpp` (`imdimage_tests`; built with `UNIT_TESTS` and `FNIO_IS_STDIO`) |
| `MediaTypeIMDTests.cpp` | `lib/media/rs232/diskTypeIMD.cpp` adapter (`mediatypeimd_tests`; built with `BUILD_RS232`) |
| `sioNetwork-DSTATS-test.cpp` | `sioNetwork` command recognition and DSTATS (`sio_dstats_tests`; ATARI target only) |
| `sioNetworkTests.cpp` | What `sioNetwork` sends to the bus for a failed CLOSE, its repeat and a failed READ (`sio_network_tests`; ATARI target only) |
| `sioNetworkStubs.cpp`, `sioNetworkStubs.h` | Link-time stubs shared by `sio_dstats_tests` and `sio_network_tests`; the `systemBus` stubs record into `bus` |
| `NQueryOutputModeTests.cpp`, `NParserReadTests.cpp` | `NParser` and `XMLParser` query output modes and short reads (`nquery_output_mode_tests`; ATARI target only) |
| `MgHttpCompletionProbe.cpp`, `mg_http_completion_test.py` | Real PC HTTP fixed-length/chunked completion, redirect connection isolation, two-request reuse and body/status/EOF (`mg_http_completion_tests`; POSIX hosts) |
| `HttpHeaderTests.cpp` | `lib/fn_esp_http_client/fn_http_header.cpp` header chunking (`http_header_tests`; skipped where the BSD queue header is missing) |
| `WebDavMethodsTests.cpp` | `lib/http/webdav/webdav_methods.h`: every method the WebDAV server handles is registered (`webdav_methods_tests`) |
| `mac_gcr_test.cpp` | `lib/media/mac/macGCR.cpp` encode and decode round trip (`mac_gcr_tests`) |
| `CasTapeTests.cpp`, `CassetteRewindRequestTests.cpp`, `CassetteTrailTests.cpp` | Atari cassette record timing, rewind request and trail (`cassette_tests`) |
| `CasFSKTests.cpp` | `lib/media/atari/casFSK.cpp` run scan, chunk checks and timing (`casfsk_tests`) |
| `CasFSKLoaderTests.cpp` | The value-to-symbol encoder in `casFSK.h` and the progressive loader in `lib/media/atari/casFSKLoader.cpp` (`casfskloader_tests`) |
| `CassetteFSKReadTests.cpp` | `lib/device/sio/cassetteFSKRead.cpp` over a fake remote file that loses its position (`cassettefskread_tests`) |
| `FSKTestImages.h` | Synthetic A8CAS images, a fake file and an encoder oracle shared by the two FSK test files above |
| `TnfsFramingTests.cpp` | `lib/TNFSlib/tnfslib_framing.cpp` reply lengths, split and coalesced streams, malformed input (`tnfs_framing_tests`) |
| `TnfsTcpRecvTests.cpp` | The real `lib/TNFSlib` client over TCP against a scripted server: split, merged, late, stale, malformed and cut-off replies, closed connections, failed MOUNT and EAGAIN; UDP and the UDP fallback unchanged (`tnfs_tcp_recv_tests`) |
| `tnfs_harness/` | Stand-ins for `fnSystem.h` (a virtual clock), `bus.h`, `fnDNS.h`, `utils.h`, `fnTcpClient.h` and `fnUDP.h`, the last two backed by the scripted peer in `harness_server.h`; used by `tnfs_tcp_recv_tests` |
| `check_no_build_ifdefs.py` | Policy: no `BUILD_*` macro in a preprocessor conditional under `lib/device/fujiDevice`, `lib/device/fujiClock` or `lib/device/NDevice` (`no_build_ifdefs_in_fujidevice`) |
| `check_no_system_bus.py` | Policy: `SYSTEM_BUS` must not appear in any source under `lib/media` (`no_system_bus_in_media`) |
| `unsit.c`, `stuffit_test.sh` | Host CLI over `lib/stuffit` and a comparison against `unar` (`stuffit_sample_tests`; registered only with `SIT_SAMPLES_DIR`) |
| `ndif_blocks_test.c`, `ndif_test.sh` | NDIF decoding compared with ndif2raw (`ndif_sample_tests`; registered only with `NDIF2RAW_DIR`) |
| `webdav_transfer_test.py`, `webdav/` | Files moving through the ESP-only WebDAV server over the real `lib/http/webdav` and Meatloaf `FlashMFile` code (`webdav_transfer_tests`; POSIX hosts). The script mirrors the sources into the build directory with `meatloaf.cpp` trimmed to the Flash filesystem; `webdav/` holds the fake `esp_http_server`, the stand-in headers and the doctest cases |
| `esp_stubs/` | Stand-ins for ESP-IDF headers so ESP-only code compiles on a host: no-op `esp_err.h` and `esp_log.h` for `http_header_tests`, and the `http_method` names and `http_method_str()` in `http_parser.h` for the WebDAV tests |
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
