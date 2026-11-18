# lib/media/rs232

Disk image formats for `BUILD_RS232`, shared by the RS-232 and `fujiversal-*` boards.

## How it fits
- `discover_mediatype()` checks for an IMD extension first, then a `.DSK` of an exact FDC+ image
  size (`MEDIATYPE_ALTAIR`), then maps XEX to `MEDIATYPE_IMG` and ROM, BIN, INT, ITV, CHF, NES,
  SMS, A78, A52, ST2, and COL to `MEDIATYPE_ROM`; `rs232Disk::mount()` in
  [lib/device/rs232/](../../device/rs232/) passes it the file size, picks the subclass and passes
  the host, or for a ROM opens a cartridge format from [lib/media/rom/](../rom/) through
  `ROMFactory`.
- `rs232Disk::write_blank()` picks the same way for NEW DISK: an IMD name gets
  `MediaTypeIMD::create()`, anything else `MediaTypeImg::create()`.
- Included by [lib/media/media.h](../media.h) under `BUILD_RS232`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_RS232`. PC: `FUJINET_TARGET=RS232`
lists every file; `tests/MediaTypeIMDTests.cpp` and `tests/IMDImageTests.cpp` cover the IMD path,
`tests/MediaTypeImgTests.cpp` the flat image and `tests/MediaTypeAltairTests.cpp` the Altair one.
