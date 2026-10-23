# lib/device/Cartridge

The cartridge behind a companion MCU. A mounted ROM is pushed into it whole as DBC `NET_OPEN`,
`NET_WRITE` blocks and `NET_CLOSE`; nothing reads it back.

## Layout

| File | Defines |
|---|---|
| `Cartridge.h`, `Cartridge.cpp` | `Cartridge`, its `Dialect` presets, `Source` and the push (`load()`) |

## How it works

- `load()` pushes an image, after its memory map if the caller has one. Each is a `Source`: a
  size and a read-by-offset function, so the cartridge never sees a file or a media type. A failed
  push fails the mount.
- A `Dialect` is what one companion firmware expects:

| Preset | `NET_OPEN` | Block | Replies | Companion | Owner |
|---|---|---|---|---|---|
| `PICO_STREAMS` | payload `[stream id][size u32le]`; map on stream 1, then the image on 0 | 512 | must ACK; a failed stream's `NET_CLOSE` carries `0x01` to abort | `pico/<console>` carts | `rs232Fuji` |
| `FUJIVERSAL` | `u16` param: load segment 0 | 256 | any reply counts; `NET_CLOSE` unchecked | `pico/fujiversal` | `drivewireFuji` |

## How it fits

- Not a bus device: the companion is `FUJI_DEVICEID::DBC` and always speaks FujiBus, whatever the
  host bus. Frames go through `SystemBusBase::sendCommand(FUJI_DEVICEID::DBC, ...)` in
  [lib/bus/](../../bus/bus.h); a bus without a companion fails it, and so the mount.
- The Fuji owns the `Cartridge`. Its `mount_media()` sends a ROM here instead of to the disk: it
  mounts the file on its platform's `MediaTypeROM` from [lib/media/](../../media/) (on rs232 that
  also finds the `.cfg` memory map), wraps `stream_size()`/`stream_read()` as `Source`s and calls
  `load()`. The `MediaTypeROM` is local, so the file closes when the push is done, the slot reads as
  empty, and the disks have no ROM code. The cartridge never needs the file again,
  so no unmount path has anything to release. CONFIG still mounts a ROM through its slot.
- Covered by `cartridge_tests` (frames byte for byte against a fake bus) and the
  `no_build_ifdefs_in_fujidevice` ctest.

## Build

Unguarded. ESP: globbed by `src/CMakeLists.txt` into every target. PC: in the common source list.
