# components_pc

Vendored libraries used only by the FujiNet-PC host build. The ESP32 build takes its equivalents
from [components/](../components/) and ESP-IDF.

## Layout
| Component | Upstream and version | Licence | How it is built | Consumer |
|---|---|---|---|---|
| `cJSON` | cJSON 1.7.18 | MIT | `add_subdirectory`; links `cjson` and `cjson_utils` | `lib/fnjson`, `lib/http` |
| `doctest` | doctest 2.4.12, single header | MIT | include path only | [tests/](../tests/) |
| `libnfs` | libnfs 16.2.0 | LGPL-2.1 | `add_subdirectory` | `lib/FileSystem/fnFsNFS.cpp` |
| `libsmb2` | libsmb2 | LGPL-2.1 | `add_subdirectory` | `lib/FileSystem/fnFsSMB.cpp` |
| `libssh` | libssh 0.11.3, full upstream tree | LGPL-2.1 | `add_subdirectory` | `lib/network-protocol/SSH.h`, `lib/network-protocol/SFTP.h`, `lib/network-protocol/SSHCopyId.cpp` |
| `miniaudio` | miniaudio 0.11.22 | public domain or MIT-0 | included directly by `lib/sam/samlib.cpp` | SAM speech output |
| `mongoose` | Mongoose 7.20 | GPL-2.0 (dual-licensed upstream) | `mongoose/mongoose.c` compiled into the executable with `MG_TLS` set to mbedTLS | `lib/http/mgHttpService.cpp`, `lib/http/mgHttpClient.cpp`, `lib/network-protocol/mgWebSocketClient.cpp`, `lib/tcpip/fnTcpClientSecure.cpp` |

## How it fits
- `fujinet_pc.cmake` adds the include paths and subdirectories above and links the results into the
  `fujinet` executable; its mbedTLS probe exists because Mongoose's TLS backend needs mbedTLS 3.x
  headers.
- The ESP32 build never sees this directory: ESP-IDF supplies cJSON, TLS and the HTTP server, and
  [components/](../components/) supplies libnfs, libsmb2 and libssh.
