# lib/console

An interactive shell on the debug UART: a vendored copy of jbtronics' ESP32Console (the version is
in `ESP32Console.h`) extended with FujiNet commands. ESP only.

## How it fits
- `src/main.cpp` creates the console and registers the groups only under `ENABLE_CONSOLE`, which
  no board ini sets by default; `include/debug.h` includes `ESP32Console.h` under the same flag.
- The VFS commands use [lib/meatloaf/](../meatloaf/) for file access.

## Build
ESP: globbed into every target, inert without `ENABLE_CONSOLE`. PC: not compiled.

## Notes
- Vendored: `cxxopts/` (cxxopts, Jarryd Beck, MIT), `improv/` (the Improv Wi-Fi C++ SDK) and
  `ute/` (uTE, a micro text editor).
