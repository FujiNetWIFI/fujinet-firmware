# lib/device/Cartridge

The cartridge behind a companion MCU. A mounted ROM is pushed into it whole; nothing reads it back.

## Layout

| File | Defines |
|---|---|
| `Cartridge.h`, `Cartridge.cpp` | `Cartridge`, which pushes a `ROMMedia` to `FUJI_DEVICEID::DBC` |

## How it fits

- Not a bus device: the companion always speaks FujiBus, whatever the host bus. Frames go through
  `SystemBusBase::sendCommand()` in [lib/bus/](../../bus/bus.h); a bus without a companion fails
  it, and so the mount.
- `fujiDevice` asks its subclass's `openROM()` for the file as a `ROMMedia` from
  [lib/media/rom/](../../media/rom/). A ROM goes here and leaves the drive empty; anything else is
  mounted in the drive.
- One class serves every platform. What differs between ROMs, such as mapper info and bank
  windows, comes from the `ROMMedia` format.

## Build

ESP: globbed by `src/CMakeLists.txt` into every target. PC: listed in `fujinet_pc.cmake` for every
target.
