# lib/config

The in-memory model of fnconfig.ini: one `fnConfig` object holds every persisted setting with
typed accessors and reads and writes the INI file, and `FNPassword` guards the web UI.

## How it fits
- `Config` is read by the buses, every device directory, [lib/http/](../http/) (the configurator
  writes it), [lib/network-protocol/](../network-protocol/) (OAuth and S3 credentials),
  [lib/device/fujiDevice/](../device/fujiDevice/) (host and mount slots) and `src/main.cpp`.
- Uses `fsFlash` and `fnSDFAT` from [lib/FileSystem/](../FileSystem/), `fnSystem` and
  `fnKeyManager` from [lib/hardware/](../hardware/), `crypto` from [lib/encrypt/](../encrypt/) to
  obfuscate stored WiFi passphrases, and `Hash` from [lib/encoding/](../encoding/) for the password
  digest.

## Build
ESP: globbed by `src/CMakeLists.txt` into every target. PC: every file is listed in
`fujinet_pc.cmake` for every target.

## Notes
- Sections and files are not one-to-one: `[BOIP]` lives in `fnc_serial.cpp`, `[TapeN]` in
  `fnc_mounts.cpp`, and the printer-enabled flag in `fnc_general.cpp`.
- `[Serial]` exists only on `BUILD_RS232` and PC builds; the serial pin enums are PC-only.
- On ESP Atari and ADAM builds, holding button B at boot deletes the config file.
- `load()` prefers the copy on SD over the copy in flash.
- The `fnPassword` digest is stamped with the firmware identity, so a re-flash clears it.
