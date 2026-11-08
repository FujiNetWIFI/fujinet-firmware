# tests/http_harness

A stand-in for the HTTP client the N: HTTP protocol uses on a host, so the real
`lib/network-protocol/HTTP.cpp` runs without a server.

## Layout
| File | Replaces | Defines |
|---|---|---|
| `mgHttpClient.h` | `lib/http/mgHttpClient.h` | `FakeHttpServer` (`requests`, `status`, `response`) and a client that records each request's method, body and headers in the one `fake_http`, answers with `status`, and reads back `response` |

## How it fits
- Used by `http_protocol_tests` (`tests/HTTPProtocolTests.cpp`), which defines `fake_http` and
  links the real `HTTP.cpp`, `FS.cpp` and `Protocol.cpp` from
  [lib/network-protocol/](../../lib/network-protocol/), with [tcp_harness/](../tcp_harness/)
  for the bus.

## Build
PC only. `tests/CMakeLists.txt` puts this directory first on the include path of
`http_protocol_tests`, so this header wins over the real one. Do not add `lib/http` to that target.

## Notes
What it does not verify: mongoose, `fnHttpClient` on ESP32, sockets, TLS, redirects or
response headers.
