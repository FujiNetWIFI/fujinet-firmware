# lib/fn_esp_http_client

A fork of the ESP-IDF `esp_http_client` component (Espressif, Apache-2.0) in `namespace fujinet`,
kept so FujiNet can send WebDAV request methods and so a wrong password is tried once rather than
retried like a redirect.

## Layout
| File | Defines |
|---|---|
| `fn_esp_http_client.h`, `fn_esp_http_client.cpp` | the client itself; local edits are marked `OMF` |
| `fn_http_auth.h`, `fn_http_auth.cpp` | Basic and Digest authorization headers |
| `fn_http_header.h`, `fn_http_header.cpp` | request and response header lists |
| `fn_http_utils.h`, `fn_http_utils.cpp` | string helpers |

## How it fits
- Its only consumer is `fnHttpClient` in [lib/http/](../http/), the ESP side of the
  `HTTP_CLIENT_CLASS` macro that network adapters and `FileSystemHTTP` use. The PC side is
  `mgHttpClient` over mongoose.

## Build
ESP only: it depends on FreeRTOS and `esp_transport`. PC: not part of the firmware build, but
`tests/CMakeLists.txt` compiles `fn_http_header.cpp` and `fn_http_utils.cpp` against
`tests/esp_stubs/` for `http_header_tests`.

## Notes
The header comment in `fn_esp_http_client.cpp` records which ESP-IDF version it was copied from
and why.
