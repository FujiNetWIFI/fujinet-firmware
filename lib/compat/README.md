# lib/compat

Portability shims so the same sources compile on ESP-IDF, Linux, macOS and Windows.

## Layout
| File | Defines |
|---|---|
| `compat_string.h`, `strlcpy.c`, `strlcat.c` | `strlcpy` and `strlcat` for Linux and Windows; the implementations are OpenBSD's (Todd C. Miller, ISC licence) |
| `compat_dirent.h`, `win32_dirent.h` | the system `<dirent.h>` on POSIX, Toni Ronkko's dirent port on Windows (MIT) |
| `compat_uname.h`, `win32_uname.c` | `uname()` and `struct utsname` for Windows |
| `compat_gettimeofday.h`, `compat_gettimeofday.c` | `compat_gettimeofday()` with a Windows implementation |
| `compat_inet.h`, `compat_inet.c` | one set of socket headers and `IPADDR_*` constants over lwIP, BSD sockets and Winsock, plus `compat_inet_ntoa`, `compat_sockstrerror` and blocking-mode helpers |
| `compat_esp.h` | `IRAM_ATTR`, `portTICK_PERIOD_MS` and friends as no-ops off the ESP |
| `linux_termios2.h` | `struct termios2` for non-standard baud rates (Matthias Reichl, GPL-2.0-or-later) |

## How it fits
- Included by anything that touches strings, sockets, directories or time on more than one
  platform: [lib/config/](../config/), [lib/FileSystem/](../FileSystem/), [lib/tcpip/](../tcpip/),
  [lib/TNFSlib/](../TNFSlib/), [lib/http/](../http/) and the per-bus Fuji devices.
- `TTYChannel` in [lib/hardware/](../hardware/) uses `linux_termios2.h` to set custom serial speeds.

## Build
ESP: the `.c` files are wrapped in `#ifndef ESP_PLATFORM`, so the glob compiles them to nothing;
only the headers matter. PC: `compat_inet.c` and `compat_gettimeofday.c` always, `strlcpy.c` and
`strlcat.c` except on macOS, `win32_uname.c` only on Windows (`fujinet_pc.cmake`).
