# lib/bus

One directory per host-computer bus. A bus owns the wire protocol and its timing and nothing else:
it receives a command frame, finds the addressed device and hands it a packet. Device behaviour
lives in [lib/device/](../device/) and image formats in [lib/media/](../media/).

## Contract

`SystemBusBase` in [bus.h](bus.h) is the transaction contract between a bus and its devices. A
device never reads or writes the wire itself; it accepts the transaction, pulls or pushes its
payload and ends the transaction exactly once.

| Member | Role |
|---|---|
| `transaction_accept(transState_t)` | acknowledge the command; the state says whether a data phase follows |
| `transaction_get(void *, size_t)`, `transaction_send(const void *, size_t, bool)` | move the payload |
| `transaction_success()`, `transaction_error()` | terminate the transaction |
| `transaction_varlen_data`, `transaction_varlen_string` | send a length-prefixed payload when the packet type supports `setDataLength()` |
| `nativeTextToUnicode`, `unicodeTextToNative`, `nativeEOL` | character-set hooks a bus overrides (PETSCII, CRLF) |
| `addDevice`, `fujiIDForDevice`, `assignFujiIDToDevice`, `setDeviceEnabled`, `rotateDevices` | the device registry, backed by a `DaisyChain` |

Command flow: hardware or a bus-over-IP socket → `systemBus::service()` → look the device ID up
in the `DaisyChain` → the per-bus `virtualDevice` entry point (`sio_process`, `iwm_ctrl`,
`processCommand` and so on) → usually `fujiDevice::processCommand()` or `NDevice::processCommand()`.

Each bus header also defines three things the rest of the tree relies on: a class named
`systemBus`, a `virtualDevice` base for devices on that bus, and the `FUJI_COMMAND_PACKET` type
that the shared device bases are written against.

## Layout

| File | Defines |
|---|---|
| `bus.h`, `bus.cpp` | `SystemBusBase`, `transState_t`, the `BUILD_*` switchboard that includes the active bus header |
| `DaisyChain.h`, `DaisyChain.cpp` | `DaisyChain`: the device list. `addDevice` pushes to the front, so the newest registration wins a duplicate ID |
| `PacketParam.h`, `PacketParamProxy.h` | typed access to packet parameters, used by the packet classes |
| `cmdFrame.h` | the legacy fixed command frame, still used by the buses that predate `FUJI_COMMAND_PACKET` |

## Buses

| Directory | Platform | Packet type | ESP | PC | Status |
|---|---|---|---|---|---|
| [sio/](sio/) | `BUILD_ATARI` Atari SIO | `FujiSIOPacket` | yes | yes | |
| [iwm/](iwm/) | `BUILD_APPLE` Apple II SmartPort and Disk II | `FujiIWMPacket` | yes | yes, SmartPort over SLIP only | |
| [drivewire/](drivewire/) | `BUILD_COCO` CoCo and Dragon DriveWire | `FujiDWPacket` | yes | yes | opcode-dispatched; does not use the `DaisyChain` |
| [rs232/](rs232/) | `BUILD_RS232` serial FujiBus, also every fujiversal board | `FujiBusPacket` | yes | yes | |
| [comlynx/](comlynx/) | `BUILD_LYNX` Atari Lynx ComLynx | `FujiLynxPacket` | yes | yes | |
| [adamnet/](adamnet/) | `BUILD_ADAM` Coleco ADAM AdamNet | `FujiAdamPacket` | yes | yes | runs its own bus task on ESP |
| [iec/](iec/) | `BUILD_IEC` Commodore serial IEC | `FujiIECPacket` | yes | no | built on the IECDevice library; disk images via `lib/meatloaf` |
| [mac/](mac/) | `BUILD_MAC` Macintosh floppy port | `FujiBusPacket` | yes | no | the ESP32 drives a Pico in `pico/mac`; no Fuji commands cross this bus |
| [cx16_i2c/](cx16_i2c/) | `BUILD_CX16` Commander X16 I2C | none | fails | no | legacy, not on `SystemBusBase` (#1658) |
| [h89/](h89/) | `BUILD_H89` Heathkit H89 | none | fails | no | legacy, not on `SystemBusBase` (#1658) |
| [rc2014bus/](rc2014bus/), [rc2014sio/](rc2014sio/) | `BUILD_RC2014` SPI or serial | none | fails | no | legacy; only `rc2014bus` is reachable from `bus.h` (#1658) |
| [s100spi/](s100spi/) | `BUILD_S100` S-100 over SPI | none | fails | no | legacy (#1658) |

The legacy buses keep their own device lists instead of `SystemBusBase` and define no
`FUJI_COMMAND_PACKET`, which is why the shared device bases fail to compile for them.

## How it fits

- `SYSTEM_BUS`, the one bus instance, is defined in [src/](../../src/); `main_setup()` and each
  platform's `<bus>Fuji::setup()` register devices on it.
- A bus reads and writes through an `IOChannel` from [lib/hardware/](../hardware/): a UART on
  the ESP32, USB CDC-ACM on the fujiversal boards, a TTY or a bus-over-IP socket on the PC.
- Adding a platform means a new directory here, one under `lib/device/`, a pinmap header and a
  board ini; see CONTRIBUTING.md before starting.

## Build

ESP: `src/CMakeLists.txt` globs every bus directory into every target, so each file guards itself
with `#ifdef BUILD_*`. `rs232/FujiBusPacket.cpp` and `iwm/IWMBusIDMap.cpp` are unguarded and
compile everywhere. PC: `fujinet_pc.cmake` adds one bus per `FUJINET_TARGET`.
