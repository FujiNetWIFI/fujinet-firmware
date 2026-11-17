# lib/FileSystem

Storage behind host slots, printers and the web file manager: one `FileSystem` base class with a
backend per storage type, plus `FileHandler`, a stdio-free file abstraction that lets network
filesystems work without a kernel VFS driver.

## How it fits
- [lib/fuji/](../fuji/) creates one backend per host slot. `fnSDFAT` and `fsFlash` are used directly
  by config, the printers, the web server, app keys and the password store.
- A device includes `fnio.h` to read an image; which file API it gets depends on the platform.
- Uses [lib/tcpip/](../tcpip/), [lib/TNFSlib/](../TNFSlib/), [lib/ftp/](../ftp/),
  [lib/webdav/](../webdav/) for `IndexParser`, the HTTP client from [lib/http/](../http/), and the
  libsmb2 and libnfs components.

## Build
ESP: globbed by `src/CMakeLists.txt` into every target; `fnFileTNFS.cpp` compiles to nothing there.
PC: `fujinet_pc.cmake` lists every file except `fnFsLittleFS.cpp` and `fnFsTNFSvfs.cpp`.

## Notes
- `is_global()` is true for `fnSDFAT` and `fsFlash`, so callers must not delete them.
- `fnio.cpp` is empty; the wrappers are inline in the header.
- `fnFsSD.*`: `fnSDFAT` is the SD card on ESP and a local directory on PC.
- `fnFsLittleFS.*` is the ESP flash file system (`FLASH_LITTLEFS`) and `fnFsSPIFFS.*` the PC one
  (`FLASH_SPIFFS`); whichever is active defines `fsFlash`.
- `fnio.h`: `fnFile` is a `FileHandler` on ATARI, APPLE, COCO, RS232, PC ADAM and PC IEC, and a
  stdio `FILE` everywhere else (`FNIO_IS_STDIO`).
- Each `FileSystemSDFAT` keeps its own `dir_open()` listing; copying `fnSDFAT` gives a second handle
  on the mounted card.
- `fnFileTNFS.*` is PC only.
