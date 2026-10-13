# tests/tnfs_harness

Stand-ins for the headers the TNFS client includes, so the real `lib/TNFSlib` transaction,
mount, file and directory code runs on a host against a scripted server.

## Layout
| File | Replaces | Defines |
|---|---|---|
| `harness_server.h` | — | `HarnessServer`, the scripted peer: `on_request` sees each request, `deliver()` queues bytes that become readable after a delay; connection and request counters |
| `fnTcpClient.h` | `lib/tcpip/fnTcpClient.h` | A TCP connection to `harness_server`: a byte stream where a read returns every byte that has arrived; `stop()` discards unread bytes; a peer close is seen once everything queued has been read |
| `fnUDP.h` | `lib/tcpip/fnUDP.h` | UDP to `harness_server`: each delivery is one datagram, read whole |
| `fnSystem.h` | `lib/hardware/fnSystem.h` | A virtual clock: `millis()` moves only when the code under test calls `delay()` or `delay_microseconds()` |
| `bus.h` | `lib/bus/bus.h` | `SYSTEM_BUS.getShuttingDown()`, always false |
| `fnDNS.h` | `lib/tcpip/fnDNS.h` | `get_ip4_addr_by_name()`, always fails; tests use a host name only as a label |
| `utils.h` | `lib/utils/utils.h` | The unused-variable pragma macros only |

## How it fits
- Used by `tnfs_tcp_recv_tests` (`tests/TnfsTcpRecvTests.cpp`), which defines the
  `fnSystem`, `SYSTEM_BUS` and `harness_server` objects and a minimal TNFS server on top of
  `harness_server`.
- That test links the real `tnfslib.cpp`, `tnfslib_dir.cpp`, `tnfslib_transport.cpp`,
  `tnfslib_codes.cpp`, `tnfslibMountInfo.cpp` and `tnfslib_framing.cpp` from
  [lib/TNFSlib/](../../lib/TNFSlib/).

## Build
PC only. `tests/CMakeLists.txt` puts this directory first on the include path of
`tnfs_tcp_recv_tests`, so these headers win over the real ones, and builds with `UNIT_TESTS`.
Do not add `lib/tcpip`, `lib/hardware`, `lib/bus` or `lib/utils` to that target. The test runs
under ctest (`./build.sh -p ATARI`, or `ctest -R tnfs_tcp_recv_tests` from `build/`).

## Notes
What it does not verify:
- real sockets, lwIP or ESP-IDF behavior, or the real `fnTcpClient` and `fnUDP`;
- timing on a device: the clock is virtual, and only the host (non-`ESP_PLATFORM`) branches of
  the receive loop are compiled;
- the keep-alive timer or any concurrent access to a mount;
- DNS, UDP loss or reordering, and TNFS servers other than the scripted one.
