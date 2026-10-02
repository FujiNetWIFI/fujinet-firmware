# lib/config

The in-memory model of fnconfig.ini: one `fnConfig` object holds every persisted setting with
typed accessors and reads and writes the INI file, and `FNPassword` guards the web UI.

## Layout
| File | Defines |
|---|---|
| `fnConfig.h`, `fnConfig.cpp` | `fnConfig` and its global `Config`; slot limits `MAX_HOST_SLOTS`, `MAX_MOUNT_SLOTS`, `MAX_PRINTER_SLOTS`; `CONFIG_FILENAME`; the per-platform `CONFIG_DEFAULT_BOIP_PORT`; the constructor sets the defaults |
| `fnc_load.cpp` | `load()`: prefers the copy on SD over the copy in flash and dispatches each section to a `_read_section_*` method |
| `fnc_save.cpp` | `save()`: serialises every section in a fixed order and writes the file once |
| `fnc_util.cpp` | `_find_section_in_line`, `_split_name_value`, `_read_line`, and the host-type and mount-mode string tables |
| `fnc_<section>.cpp` | One file per INI section with its `store_*`, `get_*` and `_read_section_*` methods: `[General]`, `[WiFi]` and `[WiFiStoredN]`, `[Bluetooth]`, `[Network]`, `[HostN]`, `[MountN]` and `[TapeN]`, `[PrinterN]`, `[Modem]`, `[Cassette]`, `[PhonebookN]`, `[CPM]`, `[ENABLE]`, `[GoogleDrive]`, `[S3]`, `[OneDrive]`, `[BOIP]` and `[Serial]` |
| `fnPassword.h`, `fnPassword.cpp` | `FNPassword` and its global `fnPassword`: a salted, iterated SHA-256 digest kept in NVS (ESP) or a file in the flash filesystem (PC), stamped with the firmware identity so a re-flash clears it; web session tokens; Basic-auth check for WebDAV; a recovery file read from the SD root |

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
