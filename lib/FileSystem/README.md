# lib/FileSystem

Storage behind host slots, printers and the web file manager: one `FileSystem` base class with a
backend per storage type, plus `FileHandler`, a stdio-free file abstraction that lets network
filesystems work without a kernel VFS driver.

## Layout
| File | Defines |
|---|---|
| `fnFS.h`, `fnFS.cpp` | `FileSystem` (abstract): `file_open` or `filehandler_open`, `exists`, `remove`, `rename`, `mkdir`, `dir_open`, `dir_read`, `dir_close`, `is_global()`; `fsType`; `fsdir_entry_t`; `MAX_PATHLEN` |
| `fnFsSD.*` | `FileSystemSDFAT` and global `fnSDFAT`: the SD card on ESP, a local directory on PC |
| `fnFsLittleFS.*`, `fnFsSPIFFS.*`, `fsFlash.h` | `FileSystemLittleFS` under `FLASH_LITTLEFS` (ESP) or `FileSystemSPIFFS` under `FLASH_SPIFFS` (PC); whichever is active defines the global `fsFlash` |
| `fnFsTNFS.*`, `fnFsTNFSvfs.*` | `FileSystemTNFS` and global `fnTNFS` over [lib/TNFSlib/](../TNFSlib/); the VFS shim registers each mount with ESP-IDF so stdio works on ESP; a `_tcp.` or `_udp.` hostname prefix picks the transport |
| `fnFsSMB.*`, `fnFsNFS.*`, `fnFsFTP.*`, `fnFsHTTP.*` | `FileSystemSMB` (libsmb2), `FileSystemNFS` (libnfs), `FileSystemFTP` over [lib/ftp/](../ftp/), `FileSystemHTTP` over index pages parsed by `IndexParser` |
| `fnFile.h`, `fnFile.cpp` | `FileHandler` (abstract): `read`, `write`, `seek`, `tell`, `flush`, `close` |
| `fnFileLocal.*`, `fnFileTNFS.*`, `fnFileSMB.*`, `fnFileNFS.*`, `fnFileMem.*`, `fnFileHTTP.*` | `FileHandler` backends: a stdio `FILE`, TNFS (PC only), SMB, NFS, an in-memory buffer, and ranged HTTP GET with read-ahead |
| `fnio.h`, `fnio.cpp` | `fnFile` and the `fnio` wrappers: `FileHandler` on ATARI, APPLE, COCO, RS232 and PC ADAM, a stdio `FILE` everywhere else (`FNIO_IS_STDIO`) |
| `fnFileCache.*` | `FileCache`: keeps a downloaded file in memory and spills it to SD past a threshold |
| `fnDirCache.*` | `DirCache`: a sorted, filtered directory listing, held in PSRAM on ESP |
| `directoryPageGroup.h` | `DirectoryPageGroup`: the paged directory wire format used by `fujiDevice` |
| `mediaTypeProxy.h` | Includes the platform's media header selected by `BUILD_*` |

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
