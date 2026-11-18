# lib/compat

Portability shims so the same sources compile on ESP-IDF, Linux, macOS and Windows.

## How it fits
- Included by anything that touches strings, sockets, directories or time on more than one
  platform: [lib/config/](../config/), [lib/FileSystem/](../FileSystem/), [lib/tcpip/](../tcpip/),
  [lib/TNFSlib/](../TNFSlib/), [lib/http/](../http/) and the per-bus Fuji devices.
- `TTYChannel` in [lib/hardware/](../hardware/) uses `linux_termios2.h` to set custom serial speeds.

## Build
ESP: the `.c` files are wrapped in `#ifndef ESP_PLATFORM`, so the glob compiles them to nothing;
only the headers matter. PC: `compat_inet.c` and `compat_gettimeofday.c` always, `strlcpy.c` and
`strlcat.c` except on macOS, `win32_uname.c` only on Windows (`fujinet_pc.cmake`).

## Notes
- `strlcpy.c` and `strlcat.c` are OpenBSD's (Todd C. Miller, ISC license).
- `win32_dirent.h` is Toni Ronkko's dirent port (MIT).
- `linux_termios2.h` is by Matthias Reichl (GPL-2.0-or-later).
