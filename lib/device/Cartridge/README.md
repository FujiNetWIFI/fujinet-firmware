# lib/device/Cartridge

The cartridge behind a companion MCU. A mounted ROM is pushed into it whole as DBC `NET_OPEN`,
`NET_WRITE` blocks and `NET_CLOSE`; nothing reads it back.

## Layout

| File | Defines |
|---|---|
| `Cartridge.h`, `Cartridge.cpp` | `Cartridge`, the base class: `is_rom()`, `mount()` and the push |

## How it works

- `fujiDevice` mounts a file into its `Cartridge`, when it has one and `is_rom()` says so, instead
  of a disk slot. `mount()` pushes the ROM, closes the file and leaves the slot empty; a failed
  push fails the mount.
- `push()` sends one stream: `NET_OPEN`, `NET_WRITE` blocks, then `NET_CLOSE`, which always goes
  out so the companion's stream state can't wedge. The base sends what `pico/fujiversal` expects:
  a `u16` load segment 0 on `NET_OPEN`, 256-byte blocks, any reply counting, `NET_CLOSE` unchecked.
- A bus whose companion wants something else overrides `block_size()`, `open()`, `accepted()` or
  `close()`:

| Subclass | Companion | Overrides |
|---|---|---|
| [rs232Cartridge](../rs232/rs232Cartridge.h) | `pico/<console>` carts | all four: `NET_OPEN` payload `[stream id][size u32le]`, 512-byte blocks, must ACK, a failed stream's `NET_CLOSE` carries `0x01` to abort; `mount()` pushes the `.cfg` memory map on stream 1 before the image on 0 |
| [drivewireCartridge](../drivewire/drivewireCartridge.h) | `pico/fujiversal` | none |

## How it fits

- Not a bus device: the companion is `FUJI_DEVICEID::DBC` and always speaks FujiBus, whatever the
  host bus. Frames go through `SystemBusBase::sendCommand(FUJI_DEVICEID::DBC, ...)` in
  [lib/bus/](../../bus/bus.h); a bus without a companion fails it, and so the mount.
- Each subclass reads the ROM through its platform's `MediaTypeROM` from
  [lib/media/](../../media/) (on rs232 that also finds the `.cfg` memory map). The `MediaTypeROM`
  is local to `mount()`, so the file closes when the push is done and the disks have no ROM code.
  CONFIG still mounts a ROM through its slot.
- The bus's Fuji owns its subclass and sets `fujiDevice::_cartridge` to it.
- Covered by `cartridge_tests` (frames byte for byte against a fake bus) and the
  `no_build_ifdefs_in_fujidevice` ctest.

## Build

Unguarded. ESP: globbed by `src/CMakeLists.txt` into every target; the subclasses come with their
bus folder. PC: in the common source list; the subclasses in their platform's list.
