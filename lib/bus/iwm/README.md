# lib/bus/iwm

Apple II SmartPort and Disk II bus for `BUILD_APPLE`. On ESP32 it drives the IWM signals directly;
on the PC build it carries SmartPort over SLIP to an emulator.

## Layout
| File | Defines |
|---|---|
| `iwm.h`, `iwm.cpp` | `virtualDevice` (SmartPort verbs `iwm_status()`, `iwm_readblock()`, `iwm_writeblock()`, `iwm_format()`, `iwm_ctrl()`, `iwm_open()`, `iwm_close()`, `iwm_read()`, `iwm_write()`, plus pure `create_dib_reply_packet()` and `create_status_reply_packet()`) and `systemBus` (`serviceSmartPort()`, `serviceDiskII()`, unit-number assignment) |
| `FujiIWMPacket.h`, `FujiIWMPacket.cpp` | `FujiIWMPacket`, aliased to `iwm_decoded_cmd_t`, this bus's `FUJI_COMMAND_PACKET` |
| `IWMBusIDMap.h`, `IWMBusIDMap.cpp` | `IWMBusIDMap`, FujiNet device ID to SmartPort unit number |
| `iwm_ll.h`, `iwm_ll.cpp`, `spi_continuous.h`, `spi_continuous.c` | `iwm_sp_ll` and `iwm_diskii_ll`, the ESP32 signal-level layer (SPI, RMT) |
| `iwm_slip.h`, `iwm_slip.cpp` | `iwm_slip`, SmartPort over SLIP using [lib/devrelay/](../../devrelay/) |
| `connector.h`, `connector_net.h`, `connector_net.cpp`, `connector_com.h`, `connector_com.cpp` | `connector` and its TCP and serial implementations for the SLIP link |
| `spCode.h`, `spCommandID.h` | SmartPort status codes and command numbers |

## How it fits
- Selected by `BUILD_APPLE` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/iwm/](../../device/iwm/).
- The SLIP path decodes requests with `Request::from_packet()` from [lib/devrelay/](../../devrelay/)
  and connects to the host named by `Config.get_boip_host()`.

## Build
ESP: globbed by `src/CMakeLists.txt`; every file compiles only under `BUILD_APPLE`. PC:
`FUJINET_TARGET=APPLE` lists `iwm`, `iwm_slip`, `FujiIWMPacket`, `IWMBusIDMap` and one connector
chosen by the CMake option `SLIP_PROTOCOL` (`NET` or `COM`); `iwm_ll` and `spi_continuous` are ESP
only.

## Notes
`IWMBusIDMap.cpp` has no build guard and is compiled into every ESP target.
