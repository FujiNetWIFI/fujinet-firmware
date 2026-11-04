# tests/tcp_harness

Stand-ins for the socket headers the N: TCP, TELNET and UDP protocols include, so the real
`lib/network-protocol/TCP.cpp`, `Telnet.cpp` and `UDP.cpp` run on a host against a fake peer.

## Layout
| File | Replaces | Defines |
|---|---|---|
| `fnTcpClient.h` | `lib/tcpip/fnTcpClient.h` | `FakePeer` (`inbound`, `received`, `write_limit`, `waiting`, `reset_on_write`, `closed`) and a client connected to the one `fake_peer`: a read returns whatever is in `inbound`, a write sends at most `write_limit` bytes or, with `reset_on_write`, nothing and stops the client; with `closed`, `connected()` is false but `inbound` can still be read, as when the peer closes after sending |
| `fnTcpServer.h` | `lib/tcpip/fnTcpServer.h` | A server that listens; `hasClient()` is `fake_peer.waiting`, and `client()` takes that caller |
| `fnUDP.h` | `lib/tcpip/fnUDP.h` | A socket holding one fake datagram, `fake_datagram` |
| `fnDNS.h` | `lib/tcpip/fnDNS.h` | The lookup `UDP.cpp` declares; the test defines it |
| `bus.h` | `lib/bus/bus.h` | Nothing; `Protocol.h` includes it |

## How it fits
- Used by `tcp_protocol_tests` (`tests/TCPProtocolTests.cpp`), `telnet_protocol_tests`
  (`tests/TelnetProtocolTests.cpp`) and `udp_protocol_tests` (`tests/UDPProtocolTests.cpp`); each defines `fake_peer` and links the real `TCP.cpp` and
  `Protocol.cpp` from [lib/network-protocol/](../../lib/network-protocol/), and the TELNET one
  `Telnet.cpp` and libtelnet from [lib/telnet/](../../lib/telnet/).

## Build
PC only. `tests/CMakeLists.txt` puts this directory first on the include path of
`tcp_protocol_tests`, `telnet_protocol_tests` and `udp_protocol_tests`, so these headers win over the real ones. Do
not add `lib/tcpip` or `lib/bus` to any of these targets.

## Notes
What it does not verify: real sockets, lwIP or ESP-IDF behavior, timeouts, or the bus side of a
read or write.
