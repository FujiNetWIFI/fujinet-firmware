# tests

Host-side unit tests and policy checks for the FujiNet-PC build. ctest runs them at the end of
`./build.sh -p <TARGET>` and in the Build FujiNet-PC workflow.

## Layout
| File | Covers |
|---|---|
| `CMakeLists.txt` | One `add_executable` and `add_test` pair per test; each links only the unit under test, never the `fujinet` executable |
| `FujiBusPacketTests.cpp` | `lib/bus/FujiBusPacket.cpp` SLIP framing (`fujibuspacket_tests`) |
| `DaisyChainTests.cpp` | `lib/bus/DaisyChain.cpp` device-ID rotation behind the disk swap (`daisychain_tests`) |
| `FnSanitizeTests.cpp` | `lib/fntext/fn_sanitize.cpp` entity decoding and UTF-8 folding (`fn_sanitize_tests`) |
| `PeoplesUrlParserTests.cpp` | `lib/utils/peoples_url_parser.cpp` query and fragment splitting, and `getFilePath()` (`url_parser_tests`) |
| `UtilsTests.cpp` | `lib/utils/utils.cpp` `util_stoi()` (`utils_tests`) |
| `FnxmlQueryTests.cpp` | `lib/fnxml/fnxml_query.cpp` XPath subset over tinyxml2 (`fnxml_query_tests`) |
| `CalendarTests.cpp` | `lib/utils/fn_time.cpp` and `lib/network-protocol/calendar_draft.cpp` (`calendar_tests`) |
| `QRCodeTests.cpp` | `lib/qrcode/qrcode.c` version and ECC selection (`qrcode_tests`) |
| `MailDraftTests.cpp` | `lib/network-protocol/mail_draft.cpp` RFC822 compose and reply (`mail_tests`) |
| `IMDImageTests.cpp`, `IMDFixture.h` | `lib/media/IMDImage.cpp` (`imdimage_tests`; built with `UNIT_TESTS` and `FNIO_IS_STDIO`) |
| `MediaTypeIMDTests.cpp` | `lib/media/rs232/diskTypeIMD.cpp` adapter (`mediatypeimd_tests`; built with `BUILD_RS232`) |
| `MediaTypeAltairTests.cpp` | `lib/media/rs232/diskTypeAltair.cpp` adapter (`mediatypealtair_tests`; built with `BUILD_RS232`) |
| `MediaTypeImgTests.cpp` | `lib/media/rs232/diskTypeImg.cpp` blank image from `create()` (`mediatypeimg_tests`; built with `BUILD_RS232`) |
| `FileSizeTests.cpp` | `file_size_t` in `include/global_types.h`: the types it converts to without a cast, and `to()` refusing -1 and a size that doesn't fit (`file_size_tests`) |
| `MediaType2MGTests.cpp` | `lib/media/apple/mediaType2MG.cpp` and `mediaTypePO.cpp` mounts, 2MG detection, the high-score marker and the 2MG header NEW DISK writes (`mediatype2mg_tests`; built with `BUILD_APPLE`) |
| `FsSDListingTests.cpp` | `lib/FileSystem/fnFsSD.cpp` listings over a local directory: a second handle's `dir_open()` leaves `fnSDFAT`'s listing alone (`fssd_listing_tests`) |
| `PDFPrinterTests.cpp` | `lib/printer-emulator/pdf_printer.cpp` page breaks when one buffer holds more than one line (`pdf_printer_tests`; not on the APPLE target) |
| `sioNetwork-DSTATS-test.cpp` | `sioNetwork` command recognition and DSTATS (`sio_dstats_tests`; ATARI target only) |
| `sioNetworkTests.cpp` | What `sioNetwork` sends to the bus for a failed CLOSE, its repeat and a failed READ (`sio_network_tests`; ATARI target only) |
| `NDeviceTests.cpp` | The shared `NDevice` without a bus subclass: a QUERY whose flags byte the parser refuses (`ndevice_tests`; ATARI target only) |
| `sioNetworkStubs.cpp`, `sioNetworkStubs.h` | Link-time stubs shared by `sio_dstats_tests`, `sio_network_tests` and `ndevice_tests`; the `systemBus` stubs record into `bus` |
| `AdamNetworkReceiveTests.cpp`, `adamNetworkReceiveStubs.cpp`, `adamNetworkReceiveStubs.h` | `lib/device/adamnet/adamNetwork.cpp` RECEIVE sending at most 1024 bytes and keeping the rest; the stubs record bus replies (`adam_network_receive_tests`; ADAM target only) |
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
| `TCPProtocolTests.cpp` | `lib/network-protocol/TCP.cpp` reads, writes, ACCEPT and CLOSE CLIENT over a fake socket (`tcp_protocol_tests`) |
| `ProtocolFSTests.cpp` | `lib/network-protocol/FS.cpp` unmounting when an open fails, keeping its error (`fs_protocol_tests`) |
| `SMBProtocolTests.cpp` | `lib/network-protocol/SMB.cpp` MKDIR and RMDIR failing on a URL with no share and on an unreachable server (`smb_protocol_tests`) |
| `TelnetProtocolTests.cpp` | `lib/network-protocol/Telnet.cpp` reads, writes and separate channels over the same fake socket (`telnet_protocol_tests`) |
| `tcp_harness/` | Stand-ins for `fnTcpClient.h`, `fnTcpServer.h` and `bus.h`, backed by the fake peer in `fnTcpClient.h`; used by `tcp_protocol_tests`, `telnet_protocol_tests`, `fs_protocol_tests` and `smb_protocol_tests` |
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
