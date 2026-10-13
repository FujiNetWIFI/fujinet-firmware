# lib/afp

Logs in to AFP servers, attaches volumes and reads and writes files on them through afpfs-ng, sharing each login and attachment between every part of FujiNet that uses it.

## Layout
| File | Defines |
|---|---|
| `afpBridge.h`, `afpBridge.c` | the C face of afpfs-ng: start the library, log in, find and attach a volume, detach, release; then stat, open, read, write, close, list, remove, rename, mkdir and rmdir on a volume; `afp_server`, `afp_volume` and `afp_file_info` stay opaque |
| `afpSession.h`, `afpSession.cpp` | `AfpSession`: one holder of one volume; `open`, `close`, `volume`, `volume_names`, `error` |
| `afpFile.h`, `afpFile.cpp` | `AfpFile`: one open file on a session's volume with its own position; `afp_stat`, `afp_list`, `afp_remove`, `afp_rename`, `afp_mkdir`, `afp_rmdir`; `AfpOpenMode`, `AfpDirEntry` |
| `afpPath.h`, `afpPath.cpp` | `afp_split_path`: a URL path into volume name and volume-relative path |

## How it fits
- Uses the afpfs-ng library in [components/afpfs-ng/](../../components/afpfs-ng/).
- Nothing uses it yet; the AFP network protocol and file system are to follow.

## Build
ESP: `src/CMakeLists.txt` globs `*.c` and `*.cpp` here into every target. PC: `fujinet_pc.cmake` builds `afpBridge.c` as the `afp_bridge` library; the C++ files are compiled only by the tests until something in `fujinet` uses them.

## Notes
- `afpBridge.c` is the only file that may include an afpfs-ng header. On Windows those headers need a compatibility layer that must not reach other code.
- afpfs-ng keeps one login per server and user and logs a server out with its last volume. `AfpSession` counts the holders of each volume so that happens only when the last one closes.
- When the server ends a session, afpfs-ng unmounts its volumes and frees their open files. `AfpSession::volume()` then returns `nullptr` and an `AfpFile` opened on that attachment refuses further use; a call already in the library at that moment is not protected.
- A read may return fewer bytes than asked for without being at the end of the file; only 0 means the end.
- afpfs-ng does not yet report a write the server refused, and passes an uninitialised directory ID when the parent of a path to create, remove or rename does not exist. Neither is worked around here.
- A failed login reports its cause: the name did not resolve, the connection failed, or the server refused the login.
- An existing login is reused only when the password matches the one it was made with.
- `AfpSession::open` and `close` hold one process-wide lock for their whole duration, including the login.
