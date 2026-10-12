# lib/afp

Logs in to AFP servers and attaches volumes through afpfs-ng, sharing each login and attachment between every part of FujiNet that uses it.

## Layout
| File | Defines |
|---|---|
| `afpBridge.h`, `afpBridge.c` | the C face of afpfs-ng: start the library, log in, find and attach a volume, detach, release; `afp_server` and `afp_volume` stay opaque |
| `afpSession.h`, `afpSession.cpp` | `AfpSession`: one holder of one volume; `open`, `close`, `volume`, `volume_names`, `error` |
| `afpPath.h`, `afpPath.cpp` | `afp_split_path`: a URL path into volume name and volume-relative path |

## How it fits
- Uses the afpfs-ng library in [components/afpfs-ng/](../../components/afpfs-ng/).
- Nothing uses it yet; the AFP network protocol and file system are to follow.

## Build
ESP: `src/CMakeLists.txt` globs `*.c` and `*.cpp` here into every target. PC: `fujinet_pc.cmake` builds `afpBridge.c` as the `afp_bridge` library; the C++ files are compiled only by the tests until something in `fujinet` uses them.

## Notes
- `afpBridge.c` is the only file that may include an afpfs-ng header. On Windows those headers need a compatibility layer that must not reach other code.
- afpfs-ng keeps one login per server and user and logs a server out with its last volume. `AfpSession` counts the holders of each volume so that happens only when the last one closes.
- When a server ends the session, the library unmounts its volumes underneath their holders; `AfpSession::volume()` then returns `nullptr`.
- A login failure is reported as access denied unless the name did not resolve or the connection itself failed.
