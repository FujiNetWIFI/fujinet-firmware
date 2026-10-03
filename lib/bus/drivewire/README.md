# lib/bus/drivewire

Tandy CoCo and Dragon DriveWire bus for `BUILD_COCO`: the DriveWire protocol over UART, USB
CDC-ACM or the Becker port (Bus-over-IP), dispatched by opcode rather than by device address.

## Layout
| File | Defines |
|---|---|
| `drivewire.h`, `drivewire.cpp` | `drivewireDevice` (base of every CoCo device), `virtualDevice` (Fuji-style devices implement `processCommand()`), and `systemBus` with one `op_*` handler per DriveWire opcode: `op_readex`, `op_write`, `op_fuji`, `op_net`, `op_cpm`, `op_clock`, `op_print`, the `op_ser*` serial family, `op_dwinit`, `op_namedobj_mnt` and the rest |
| `FujiDWPacket.h`, `FujiDWPacket.cpp` | `FujiDWPacket`, this bus's `FUJI_COMMAND_PACKET` |
| `opcode.h` | the DriveWire opcode numbers |

## How it fits
- Selected by `BUILD_COCO` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/drivewire/](../../device/drivewire/).
- There is no daisy chain: disks are reached as `theFuji->get_disk(n)->disk_dev`, `op_net` creates
  one `drivewireNetwork` per unit on first use, `op_clock` calls `platformClock` directly and the
  printer is handed over with `setPrinter()`. `rotateDevices()` only queues a disk swap, which
  `service()` hands to `drivewireFuji::rotate_disks()` between requests.
- Under `PINMAP_FUJIVERSAL_DRIVEWIRE` the bus also exchanges `FujiBusPacket` frames from
  [lib/bus/rs232/](../rs232/) with the RP2350 cartridge over USB.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_COCO`; `FUJINET_OVER_USB` is
derived from the pinmap's UART assignment. PC: `FUJINET_TARGET=COCO` lists both source files and
uses `BoIPChannel`.

## Notes
`BUILD_COCO` is the one platform where the native wire types `u16ne_t`, `u24ne_t` and `u32ne_t`
in `include/global_types.h` are big-endian.
