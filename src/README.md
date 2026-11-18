# src

The firmware entry point. `main.cpp` builds one platform's device set on the single bus instance
and runs the service loop; everything it assembles comes from [lib/](../lib/).

## How it works

- `main_setup()` brings the system up in a fixed order: hardware version and GPIO, keys and LEDs,
  `fsFlash`, `fnSDFAT`, the passphrase key, `Config.load()`, the web password, the optional
  companion-MCU update, then a `#ifdef BUILD_*` block that creates the platform's devices and
  registers them on `SYSTEM_BUS`, and finally `SYSTEM_BUS.setup()`. Which devices a block registers
  itself and which it leaves to `<bus>Fuji::setup()` varies by platform.
- `fn_service_loop()` starts WiFi (or Bluetooth), mounts the configured images when CONFIG boot is
  disabled, then loops over `SYSTEM_BUS.service()`. On the PC it also services `fnHTTPD` and
  `taskMgr`. The loop has no delay, so per-iteration logging is rate limited (see CONTRIBUTING).
- On ESP-IDF, `app_main()` creates the `fnLoop` task pinned to the second core and deletes
  itself; `main_setup()` runs inside that task. AdamNet runs its bus service in a task of its own
  and the loop skips `SYSTEM_BUS.service()` for it.
- On the PC, `main()` calls `main_setup()` then `fn_service_loop()`; exiting with status 75 asks
  the `run-fujinet` launcher to restart the process.

## How it fits

- The only file allowed to include `lib/device/device.h`, because that header defines objects.
- Depends on [lib/bus/](../lib/bus/), [lib/device/](../lib/device/), [lib/config/](../lib/config/),
  [lib/hardware/](../lib/hardware/), [lib/FileSystem/](../lib/FileSystem/), [lib/http/](../lib/http/)
  and, on the PC, [lib/task/](../lib/task/).

## Build

ESP: `src/CMakeLists.txt` is read by PlatformIO's ESP-IDF integration only to discover sources,
includes and flags; linking is done by PlatformIO's own build. PC: `fujinet_pc.cmake` compiles
`main.cpp` with its explicit source lists.

## Notes

- The per-directory `*.cpp` globs in `CMakeLists.txt` decide which `lib/` code the ESP build
  compiles. It also adds `fn_pico_blob_data.cpp`, generated in the build directory.
- `idf_component.yml`'s dependencies are fetched into `managed_components/`.
