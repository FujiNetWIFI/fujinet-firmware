# mcuconfig

A curses TUI for viewing and editing the GPIO pin assignment of each board, and for scaffolding
the files a new board needs.

## Layout
| File | Defines |
|---|---|
| `mcuconfig.py` | Entry point. `CONFIG` names the directories it reads; `--dump` prints every known board instead of opening the TUI |
| `mcu_manager.py` | `MCUManager`: loads every board ini, pinmap header and bus or capability definition, and checks that each signal belongs to exactly one bus or capability |
| `mcu.py` | `MCU`: one board's GPIO map; `save()` writes the files listed under How it fits |
| `mcu_editor.py`, `bus_editor.py` | The board editor form; `BusEditor` is a stub |
| `pinmap_header.py` | `PinmapHeader`: reads a pinmap header by running the C preprocessor with its `PINMAP_*` macro defined, and writes one back |
| `platformio_ini.py` | `PlatformIOIni`: reads and writes a board ini under `build-platforms/` |
| `sdkconfig.py` | `SDKConfig`: writes the board's `sdkconfig.<board>` for ESP32-S3 boards, forcing octal PSRAM on |
| `webui_config.py` | `WebUIConfig`: reads and writes a board yaml under `data/webui/config/` |
| `tui.py` | The curses form toolkit (GPLv3, Chris Osborn) |
| `board-builder.py` | Standalone report of bus-to-pin tables across all boards; the TUI does not use it |

## How it fits
- Reads [build-platforms/](../build-platforms/) inis, [include/pinmap/](../include/pinmap/) headers,
  `boards/buses.json` and `boards/capabilities.json`, and the yamls in
  [data/webui/config/](../data/webui/config/).
- Saving a board writes its pinmap header (and adds the include to `include/pinmap.h`), its board
  ini, its `sdkconfig.<board>` and its web UI yaml. It does not create a `boards/` JSON.

## Build
Not part of any build. Run it from the repository root, because every path in `CONFIG` is relative:

```sh
python3 mcuconfig/mcuconfig.py
python3 mcuconfig/mcuconfig.py --dump
python3 mcuconfig/board-builder.py build-platforms include/pinmap
```

## Notes
Needs `pyyaml`. `sdkconfig.py` also imports `kconfiglib`, which is not in `python_modules.txt`.
Writing an ini goes through `configparser`, which drops comments, and `PlatformIOIni` removes the
`build_board` key because it treats the file name as the board name.
