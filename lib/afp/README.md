# lib/afp

Logs in to AFP servers, attaches volumes and reads and writes files on them through afpfs-ng, sharing each login and attachment between every part of FujiNet that uses it.

## How it fits
- Uses the afpfs-ng library in [components/afpfs-ng/](../../components/afpfs-ng/).
- Used by `NetworkProtocolAFP` in [lib/network-protocol/](../network-protocol/) and by `FileSystemAFP` and `FileHandlerAFP` in [lib/FileSystem/](../FileSystem/).

## Build
ESP: `src/CMakeLists.txt` globs `*.c` and `*.cpp` here into every target. PC: `fujinet_pc.cmake` builds `afpBridge.c` as the `afp_bridge` library and lists the C++ files among `fujinet`'s sources.

## Notes
- `afpBridge.c` is the only file that may include an afpfs-ng header. On Windows those headers need a compatibility layer that must not reach other code.
- afpfs-ng keeps one login per server and user and logs a server out with its last volume. `AfpSession` counts the holders of each volume so that happens only when the last one closes.
- When the server ends a session, afpfs-ng unmounts its volumes and frees their open files. `AfpSession::volume()` then returns `nullptr` and an `AfpFile` opened on that attachment refuses further use; a call already in the library at that moment is not protected.
- A read may return fewer bytes than asked for without being at the end of the file; only 0 means the end.
- A failed login reports its cause: the name did not resolve, the connection failed, or the server refused the login.
- An existing login is reused only when the password matches the one it was made with.
- `AfpSession::open` and `close` hold one process-wide lock for their whole duration, including the login.
