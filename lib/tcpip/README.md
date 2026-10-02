# lib/tcpip

Thin socket wrappers with one API over lwIP on the ESP32 and BSD sockets or Winsock on the PC.

## Layout
| File | Defines |
|---|---|
| `fnTcpClient.h`, `fnTcpClient.cpp` | `fnTcpClient`: connect by name or address, buffered `read`, `read_until`, `write`, `available`; a modified ESP-Arduino `WiFiClient` |
| `fnTcpServer.h`, `fnTcpServer.cpp` | `fnTcpServer`: listen, `hasClient`, `accept` into an `fnTcpClient` |
| `fnUDP.h`, `fnUDP.cpp` | `fnUDP`: unicast and multicast datagrams with `beginPacket`/`endPacket` and `parsePacket`; from Arduino `WiFiUDP`, buffered with `cbuf` from [lib/utils/](../utils/) |
| `fnDNS.h`, `fnDNS.cpp` | `get_ip4_addr_by_name` |
| `fnTcpClientSecure.h`, `fnTcpClientSecure.cpp` | `fnTcpClientSecure`: a blocking TLS client with `readLine` and `readN`, over esp-tls on ESP and mongoose on PC |

## How it fits
- Used by the TCP, UDP, Telnet and SSH adapters in [lib/network-protocol/](../network-protocol/),
  the FTP client in [lib/ftp/](../ftp/), [lib/TNFSlib/](../TNFSlib/), every platform's modem and
  netstream device, and the bus-over-IP channels in [lib/hardware/](../hardware/) and
  [lib/bus/sio/](../bus/sio/).
- `fnTcpClientSecure` has a single consumer, the IMAPS mailbox adapter.
- Socket portability macros come from `lib/compat/compat_inet.h`.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
