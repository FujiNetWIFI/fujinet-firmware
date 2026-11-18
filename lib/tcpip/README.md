# lib/tcpip

Thin socket wrappers with one API over lwIP on the ESP32 and BSD sockets or Winsock on the PC.

## How it fits
- Used by the TCP, UDP, Telnet and SSH adapters in [lib/network-protocol/](../network-protocol/),
  the FTP client in [lib/ftp/](../ftp/), [lib/TNFSlib/](../TNFSlib/), every platform's modem and
  netstream device, and the bus-over-IP channels in [lib/hardware/](../hardware/) and
  [lib/bus/sio/](../bus/sio/).
- `fnTcpClientSecure` has a single consumer, the IMAPS mailbox adapter.
- Socket portability macros come from `lib/compat/compat_inet.h`.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.

## Notes
- `fnTcpClient` is a modified ESP-Arduino `WiFiClient`; `fnUDP` comes from Arduino `WiFiUDP`.
