# lib/telnet

libtelnet (Sean Middleditch, public domain; the version is in `libtelnet.h`), the TELNET protocol
state machine used wherever FujiNet speaks to a telnet peer.

## Layout
| File | Defines |
|---|---|
| `libtelnet.h`, `libtelnet.c` | the callback-driven `telnet_t` parser and option negotiation, unmodified apart from platform includes |

## How it fits
- The `TELNET:` adapter in [lib/network-protocol/](../network-protocol/) and every platform's
  Hayes modem device under [lib/device/](../device/) wrap it around an `fnTcpClient`
  from [lib/tcpip/](../tcpip/).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`. The optional `HAVE_ZLIB` compression
path is not enabled by either build.
