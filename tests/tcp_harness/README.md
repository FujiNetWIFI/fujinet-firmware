# tests/tcp_harness

Stand-ins for the socket headers the N: TCP, TELNET and UDP protocols include, so the real
`lib/network-protocol/TCP.cpp`, `Telnet.cpp` and `UDP.cpp` run on a host against a fake peer.

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
