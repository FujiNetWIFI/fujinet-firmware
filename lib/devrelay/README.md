# lib/devrelay

SmartPort-style device requests carried over SLIP-framed TCP or serial, so FujiNet-PC for the
Apple II talks to an emulator such as AppleWin instead of to IWM hardware.

## Layout
| File | Defines |
|---|---|
| `types/Command.h`, `types/Request.h`, `types/Request.cpp`, `types/Response.h`, `types/Response.cpp` | the command numbers (`CMD_STATUS` through `CMD_WRITE`), the `Command` base with its sequence number, `Request` with the `from_packet` factory, and `Response` |
| `commands/` | one request and response pair per command: Status, ReadBlock, WriteBlock, Format, Control, Init, Open, Close, Read, Write |
| `slip/SLIP.h`, `slip/SLIP.cpp` | `SLIP::encode`, `decode` and `split_into_packets`; the `SLIP_END` and `SLIP_ESC` bytes |
| `service/Connection.h`, `service/Connection.cpp` | the abstract connection with its reader thread and `wait_for_request` / `wait_for_response` |
| `service/TCPConnection.*`, `service/COMConnection.*` | the TCP (`SLIP_PROTOCOL_NET`) and serial (`SLIP_PROTOCOL_COM`) transports |
| `service/Listener.*`, `service/Requestor.*` | the requesting side of the protocol (a TCP listener and a request sender); compiled but unused by FujiNet |
| `util.h`, `util.cpp` | `hexDump` |

## How it fits
- [lib/bus/iwm/](../bus/iwm/) is the consumer: `lib/bus/iwm/iwm_slip.cpp` decodes incoming packets
  with `Request::from_packet`, and `lib/bus/iwm/connector_net.cpp` or `lib/bus/iwm/connector_com.cpp` opens a
  `TCPConnection` or `COMConnection` to the host and port from `Config`.
- The transport is chosen at configure time by the cmake cache option `SLIP_PROTOCOL` (NET or COM).

## Build
Every source is wrapped in `#ifdef DEV_RELAY_SLIP`. PC: `fujinet_pc.cmake` defines it for every
target but lists these files only for `FUJINET_TARGET=APPLE`. ESP: globbed, but `DEV_RELAY_SLIP`
is never defined there, so the files compile to nothing.
