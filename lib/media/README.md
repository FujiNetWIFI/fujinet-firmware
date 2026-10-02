# lib/media

Disk, tape and ROM image formats, one directory per platform. A media class knows how to read and
write sectors or blocks of one file format; it knows nothing about the bus (a ctest fails any
`SYSTEM_BUS` reference under this directory) and reaches files only through `fnio` or `FILE`.

## Layout

| File | Defines |
|---|---|
| `media.h` | the `BUILD_*` switchboard that includes the active platform's media headers |
| `IMDImage.h`, `IMDImage.cpp` | `IMDImage`, a platform-independent ImageDisk reader and writer. It is deliberately not a `MediaType`; a platform wraps it in an adapter (`rs232/diskTypeIMD.h`) |

## Platforms

There is no shared `MediaType` base class. Each directory declares its own `MediaType` and
`mediatype_t` enum, with a `mount()`, `read()`, `write()`, `status()` family shaped for that
bus (sector or block, `fnFile` or `FILE`) and a static `discover_mediatype(filename)` that maps a
file extension to the enum.

| Directory | Platform | Base header | Formats | ESP | PC |
|---|---|---|---|---|---|
| [atari/](atari/) | `BUILD_ATARI` | `atari/diskType.h` | ATR, ATX, XEX (COM, BIN); CAS and WAV tape | yes | yes |
| [apple/](apple/) | `BUILD_APPLE` | `apple/mediaType.h` | PO (HDV, 2MG), DO, WOZ, DSK | yes | yes |
| [adam/](adam/) | `BUILD_ADAM`, also `BUILD_S100` | `adam/mediaType.h` | DDP, DSK, ROM | yes | yes |
| [drivewire/](drivewire/) | `BUILD_COCO` | `drivewire/mediaType.h` | DSK, MRM, VDK, ROM (CCC), CAS presented as a disk | yes | yes |
| [rs232/](rs232/) | `BUILD_RS232` | `rs232/diskType.h` | IMG, IMD, ROM (BIN, INT, ITV, CHF) | yes | yes |
| [lynx/](lynx/) | `BUILD_LYNX` | `lynx/mediaType.h` | ROM | yes | yes |
| [mac/](mac/) | `BUILD_MAC` | `mac/mediaType.h` | MOOF, DCD (HD20), DiskCopy and raw floppy images, StuffIt archives | yes | no |
| [cbm/](cbm/) | `BUILD_IEC` | `cbm/mediaType.h` | type detection only; images are handled by `lib/meatloaf` | yes | no |
| [cx16/](cx16/) | `BUILD_CX16` | `cx16/mediaType.h` | none | legacy | no |
| [h89/](h89/), [rc2014/](rc2014/) | `BUILD_H89`, `BUILD_RC2014` | `h89/mediaType.h`, `rc2014/mediaType.h` | IMG (RomWBW geometries) | legacy | no |
| [s100spi/](s100spi/) | none | `s100spi/mediaType.h` | DSK | unreachable: `media.h` gives `BUILD_S100` the `adam/` headers | no |

## How it fits

- `DISK_DEVICE::mount()` in [lib/device/](../device/) calls the platform's `discover_mediatype()`
  and instantiates the matching subclass; `fujiDevice::fujicore_mount_disk_image_success()` in
  [lib/device/fujiDevice/](../device/fujiDevice/) drives that from a `fujiDisk` slot.
- The file handle comes from `fujiHost::fnfile_open()` in [lib/fuji/](../fuji/), so a media
  class works the same over SD, TNFS, SMB, NFS, FTP or HTTP.
- Big buffers (ATX tracks, directory caches, IMD images) use `PSRAMAllocator` from
  [include/](../../include/) on the ESP32.

## Build

ESP: `src/CMakeLists.txt` globs `lib/media/*.cpp` and `lib/media/**/*.cpp` into every target;
each platform file guards itself with `#ifdef BUILD_*`. `IMDImage.cpp` and `mac/macGCR.cpp` are
unguarded. PC: `fujinet_pc.cmake` compiles `IMDImage` for every target and one platform
directory per `FUJINET_TARGET`. `tests/` covers `IMDImage`, the rs232 IMD adapter, `macGCR` and
the Atari cassette helpers.
