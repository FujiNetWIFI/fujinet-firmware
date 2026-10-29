# lib/oled

SSD1306 128x64 I2C OLED boot splash and runtime status display for
`fujinet-coco-devkitc-centipede`, built behind `ENABLE_OLED_DISPLAY`.

## Layout
| File | Defines |
|---|---|
| `ssd1306.h`, `ssd1306.cpp` | `SSD1306`: I2C transport, init sequence, page-addressed framebuffer, GDDRAM flush |
| `oled_display.h`, `oled_display.cpp` | `OledDisplay`/`fnOledDisplay`: text rendering, the boot splash, and the status view, built on `SSD1306` |
| `font5x7.h` | `font5x7[]`, a 5x7 bitmap font (0x20-0x7E) rasterized locally from DejaVu Sans Mono Bold |
| `font7x10.h` | `font7x10[]`, the same typeface rasterized natively at 7x10 (16-bit columns) for the status row |
| `splash_bitmap.h` | `splash_icon_bitmap[]`, the FujiNet icon mark converted to a 48x48 1bpp bitmap |
| `rom_icon_bitmap.h` | `rom_icon_bitmap[]`, a hand-drawn 8x8 chip glyph shown next to a loaded ROM's name |

## How it fits
- `src/main.cpp` calls `fnOledDisplay.start()` once from `main_setup()`, guarded by
  `ENABLE_OLED_DISPLAY`. `start()` draws the boot splash, then starts a low-priority task that
  polls every 250ms and switches to the status view once a real disk is mounted *or* there's a
  recent activity event (see below) to show -- browsing a host or working with WiFi normally
  happens before anything is mounted, which is exactly when a status view is most useful, so
  mounted-disk status isn't the only reason to leave the splash. Once left, the status view stays
  up permanently; it does not revert to the splash once idle, only `reset_to_boot_logo()` does
  that.
- While any disk slot holds a ROM, `redraw_rom_status()` takes over the whole panel instead of the
  normal status view -- there's no disk traffic left to show once a ROM is loaded, so the icon,
  the ROM's name, and a two-line `font7x10` "POWER CYCLE" / "FOR CONFIG" reminder each get real
  room instead of sharing a row with the disk list.
- Otherwise the status view mixes two fonts: content rows (one per mounted slot, 8px tall with
  `font5x7`, up to 6 fit above the fixed bottom row) show `T:FILENAME` for a disk, where `T` is a
  one-letter host tag -- `S`=SD, `T`=TNFS, `H`=HTTP, `M`=SMB, `N`=NFS, `F`=FTP.
  `format_short_name()` drops any directory `fujiDisk::filename` carries (it's set from a full
  devicespec path, not a bare name) and truncates to the row's budget, keeping the extension where
  it fits. The fixed bottom row uses `font7x10` instead, via `draw_status_text()`, since it's meant
  to be read at a glance: a real mid-size font reads better there than a nearest-neighbor-scaled
  `font5x7`, and costs only one 16px row instead of three. It shows the most recent sector
  read/write and is sticky -- it stays up until the next transfer or a reboot, not a timeout.
- Mounted disk names, the most recent sector read/write, and a loaded ROM's name are read straight
  from `drivewireFuji`'s disk slots (`theFuji`, [lib/device/drivewire/](../device/drivewire/)) and
  from the DriveWire bus's last-sector-op state (`SYSTEM_BUS.get_last_sector_op()`,
  [lib/bus/drivewire/](../bus/drivewire/)) on each poll, so mounting, unmounting, rotating a disk
  or pushing a ROM needs no call into this class.
- One opportunistic activity block (`show_activity()`/`show_browsing()`) covers DriveWire network
  access, SD/TNFS directory browsing, and WiFi scan/connect status -- whichever happened most
  recently, taking whatever content rows aren't already in use by a mounted disk. It's sticky, like
  the bottom sector-activity row: it stays up until the next activity event replaces it, not on a
  timer. `show_activity()` is always one line; `show_browsing()` shows the whole path (the host tag
  plus the full `dirpath`, or the host's name for the server/card root), word-wrapped by
  `wrap_text()` at `ACTIVITY_LINE_CHARS` across as many lines as it needs and as many rows as are
  free, unlike the mounted-disk list, which stays to a short name since it's naming something
  specific rather than somewhere being navigated. None of these have persistent state to poll, so
  each is a push, and each is covered at every place that can trigger it -- there's no one shared
  code path, since the on-device FUJI command protocol, the web UI, and the serial console each
  call into WiFi/directory operations independently:
  - `drivewireNetwork::fujidev_open()` ([lib/device/drivewire/drivewireNetwork.cpp](../device/drivewire/drivewireNetwork.cpp))
    on a successful protocol open (FUJI command only). Needed `NDevice::fujidev_open()`
    ([lib/device/NDevice/NDevice.h](../device/NDevice/NDevice.h)) to become virtual.
  - `drivewireFuji::fujicore_open_directory_success()` on a directory open issued over the FUJI
    command protocol, and `fnHttpBrowse::render_hostdir()`
    ([lib/http/httpServiceBrowse.cpp](../http/httpServiceBrowse.cpp)) on one issued from the web
    UI's mount-a-disk browser. Needed `fujiDevice::fujicore_open_directory_success()`'s 3-arg
    overload ([lib/device/fujiDevice/fujiDevice.h](../device/fujiDevice/fujiDevice.h)) to become
    virtual; `httpServiceBrowse.cpp` is shared code, so its call is a plain
    `#ifdef ENABLE_OLED_DISPLAY` rather than a hook, the same as `src/main.cpp`'s.
  - WiFi scan and connect attempts, success and failure: `drivewireFuji::fujicmd_net_scan_networks()`
    (already virtual) and `fujicore_net_set_ssid_success()` (needed to become virtual) for the FUJI
    command protocol; `h_wifi_scan()` ([lib/http/httpServiceApi.cpp](../http/httpServiceApi.cpp))
    for the web UI's scan button (it has no live connect action -- WiFi credentials are set at
    initial device setup, not from the running web UI); and the `scan`/`connect` console commands
    ([lib/console/Commands/NetworkCommands.cpp](../console/Commands/NetworkCommands.cpp)) for the
    serial console, useful during board bring-up.
  The `drivewireFuji` overrides live in [lib/device/drivewire/drivewireFuji.cpp](../device/drivewire/drivewireFuji.cpp),
  the same platform-hook mechanism `fujiDevice::mount_media()` already uses; the web UI and console
  call sites are plain `#ifdef`s, since that code is already shared and platform-generic.
- `systemBus::op_reset()` ([lib/bus/drivewire/drivewire.cpp](../bus/drivewire/drivewire.cpp)) calls
  `reset_to_boot_logo()` so power-cycling back to CONFIG returns the splash, and clears the sticky
  sector-activity row so it doesn't carry stale state into the next boot.
- Pins (`PIN_OLED_SDA`, `PIN_OLED_SCL`) come from [include/pinmap/coco_devkitc.h](../../include/pinmap/coco_devkitc.h).
  `SSD1306` owns `I2C_NUM_1`, leaving `I2C_NUM_0` free for the unrelated CX16 slave-mode driver in
  [lib/bus/cx16_i2c/](../bus/cx16_i2c/).
- Unrelated to [lib/display/](../display/), the WS2812B LED-strip driver (`Display`/`DISPLAY`).

## Build
ESP only: listed in [src/CMakeLists.txt](../../src/CMakeLists.txt), which every ESP board builds
from. `ssd1306.cpp` and `oled_display.cpp` are whole-file guarded by `ENABLE_OLED_DISPLAY` (set
only in `build-platforms/platformio-fujinet-coco-devkitc-centipede.ini`), so they compile to an
empty translation unit for every other board. Not compiled on PC.

## Notes
The status view has no icon for a mounted disk, only for a loaded ROM. Flush cadence is fixed at
250ms; nothing coalesces repeated identical frames, since a full flush is cheap relative to that
interval. `font7x10` and `draw_status_text()` exist solely for the fixed bottom row and the ROM
view; a first pass that scaled `font5x7` to 2x everywhere instead looked blocky and left room for
only 3 content rows. If more than one slot somehow holds a ROM at once, `redraw_rom_status()`
shows only the first one found.

Improv WiFi provisioning (`lib/console/Commands/NetworkCommands.cpp`'s `improv_c()`) isn't hooked:
its `connectWifi()` is an unported Arduino-era stub that always returns success without touching
WiFi, so there's nothing real yet to report. Hook it if that command is ever implemented. The
WiFi auto-reconnect at boot (`src/main.cpp`, `fnWiFi.connect()` with no arguments) isn't hooked
either -- it runs before `fnOledDisplay.start()` does, too early for `show_activity()` to do
anything.
