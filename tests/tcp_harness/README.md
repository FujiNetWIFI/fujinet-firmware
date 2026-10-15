# tests/tcp_harness

Stand-ins for the socket headers the N: TCP and TELNET protocols include, so the real
`lib/network-protocol/TCP.cpp` and `Telnet.cpp` run on a host against a fake peer.

## Layout
| File | Replaces | Defines |
|---|---|---|
| `fnTcpClient.h` | `lib/tcpip/fnTcpClient.h` | `FakePeer` (`inbound`, `received`, `write_limit`, `waiting`) and a client connected to the one `fake_peer`: a read returns whatever is in `inbound`, a write sends at most `write_limit` bytes |
| `fnTcpServer.h` | `lib/tcpip/fnTcpServer.h` | A server that listens; `hasClient()` is `fake_peer.waiting`, and `client()` takes that caller |
| `bus.h` | `lib/bus/bus.h` | Nothing; `Protocol.h` includes it |

## How it fits
- Used by `tcp_protocol_tests` (`tests/TCPProtocolTests.cpp`), which defines `fake_peer` and
  links the real `TCP.cpp`, `Telnet.cpp` and `Protocol.cpp` from
  [lib/network-protocol/](../../lib/network-protocol/) and libtelnet from [lib/telnet/](../../lib/telnet/).

## Build
PC only. `tests/CMakeLists.txt` puts this directory first on the include path of
`tcp_protocol_tests`, so these headers win over the real ones. Do not add `lib/tcpip` or
`lib/bus` to that target.

## Notes
What it does not verify: real sockets, lwIP or ESP-IDF behavior, timeouts, or the bus side of a
read or write.
