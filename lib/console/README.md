# lib/console

An interactive shell on the debug UART: a vendored copy of jbtronics' ESP32Console (the version is
in `ESP32Console.h`) extended with FujiNet commands. ESP only.

## Layout
| File | Defines |
|---|---|
| `Console.h`, `Console.cpp` | `ESP32Console::Console`: the linenoise REPL task over `esp_console`, `registerCommand`, and the `register*Commands` groups |
| `ConsoleCommandBase.h`, `ConsoleCommand.h`, `ConsoleCommandD.*`, `OptionsConsoleCommand.*` | command wrappers; `OptionsConsoleCommand` parses arguments with cxxopts |
| `Commands/` | the command groups: core (clear, echo, history, env, led), system (restart, sysinfo, meminfo, ps, date), network (ping, ipconfig, scan, connect, improv), VFS (cat, ls, cd, cp, mv, rm, mkdir, edit, mount, wget), GPIO, and XFER (rx, tx) |
| `Helpers/` | working-directory and `$VAR` line interpolation helpers |
| `cxxopts/` | cxxopts, header-only option parser (Jarryd Beck, MIT) |
| `improv/` | the Improv Wi-Fi C++ SDK, behind the `improv` serial provisioning command |
| `ute/` | uTE, a micro text editor, behind the `edit` command |

## How it fits
- `src/main.cpp` creates the console and registers the groups only under `ENABLE_CONSOLE`, which
  no board ini sets by default; `include/debug.h` includes `ESP32Console.h` under the same flag.
- The VFS commands use [lib/meatloaf/](../meatloaf/) for file access.

## Build
ESP: globbed into every target, inert without `ENABLE_CONSOLE`. PC: not compiled.
