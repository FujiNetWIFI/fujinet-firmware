# lib/media/rs232

Disk and ROM image formats for `BUILD_RS232`, shared by the RS-232 and `fujiversal-*` boards.

## Layout
| File | Defines |
|---|---|
| `diskType.h`, `diskType.cpp` | this platform's `MediaType` base with 32-bit sector numbers, and `discover_mediatype()` |
| `diskTypeImg.h`, `diskTypeImg.cpp` | `MediaTypeImg`, flat sector images |
| `diskTypeIMD.h`, `diskTypeIMD.cpp` | `MediaTypeIMD`, an adapter that serves an ImageDisk image through the shared [IMDImage.h](../IMDImage.h) reader with its native per-track geometry |
| `diskTypeROM.h`, `diskTypeROM.cpp` | `MediaTypeROM`, cartridge images read by offset (Intellivision, Atari 2600, Channel F, Sega Master System, CoCo), with an optional memory-map sibling opened through the mounting `fujiHost` |

## How it fits
- `discover_mediatype()` checks for an IMD extension first, then maps XEX to `MEDIATYPE_IMG` and
  ROM, BIN, INT, ITV, CHF and SMS to `MEDIATYPE_ROM`; `rs232Disk::mount()` in
  [lib/device/rs232/](../../device/rs232/) picks the subclass and passes the host.
- Included by [lib/media/media.h](../media.h) under `BUILD_RS232`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_RS232`. PC: `FUJINET_TARGET=RS232`
lists every file; `tests/MediaTypeIMDTests.cpp` and `tests/IMDImageTests.cpp` cover the IMD path.
