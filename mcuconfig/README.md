# mcuconfig

A curses TUI for viewing and editing the GPIO pin assignment of each board, and for scaffolding
the files a new board needs.

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
`sdkconfig.py` forces octal PSRAM on for every ESP32-S3 board it writes. `BusEditor` in
`bus_editor.py` is a stub, and the TUI does not use `board-builder.py`. `tui.py` is Chris
Osborn's, GPLv3.
