# lib/devrelay

SmartPort-style device requests carried over SLIP-framed TCP or serial, so FujiNet-PC for the
Apple II talks to an emulator such as AppleWin instead of to IWM hardware.

## How it fits
- [lib/bus/iwm/](../bus/iwm/) is the consumer: `lib/bus/iwm/iwm_slip.cpp` decodes incoming packets
  with `Request::from_packet`, and `lib/bus/iwm/connector_net.cpp` or `lib/bus/iwm/connector_com.cpp` opens a
  `TCPConnection` or `COMConnection` to the host and port from `Config`.
- The transport is chosen at configure time by the cmake cache option `SLIP_PROTOCOL` (NET or COM).

## Build
Every source is wrapped in `#ifdef DEV_RELAY_SLIP`. PC: `fujinet_pc.cmake` defines it for every
target but lists these files only for `FUJINET_TARGET=APPLE`. ESP: globbed, but `DEV_RELAY_SLIP`
is never defined there, so the files compile to nothing.

## Notes
- `service/Listener.*` and `service/Requestor.*`, the requesting side of the protocol, are compiled
  but unused by FujiNet.
