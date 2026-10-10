# lib

Nearly all FujiNet code lives here. `src/main.cpp` assembles one platform's bus and device set
from these libraries; everything below the device layer is shared by every platform.

## Layers

Each layer talks only to the one beneath it. A device never touches GPIO or a socket directly;
it goes through the bus, the slot model, the filesystem or protocol adapters, and the hardware
wrappers. Reviewers hold changes to this split (see [CONTRIBUTING.md](../CONTRIBUTING.md)).

```
host computer (Atari, Apple II, CoCo, ADAM, Lynx, C64, Mac, RS232 and the fujiversal boards)
        |
lib/bus/<bus>            wire protocol and timing: systemBus, FUJI_COMMAND_PACKET, DaisyChain
        |
lib/device/<bus>         device behaviour for that bus (disk, printer, modem, network, clock ...)
lib/device/fujiDevice    shared bases the per-bus devices derive from: fujiDevice (CONFIG
lib/device/NDevice       commands plus mixins), NDevice (the N: network device), fujiClock
lib/device/fujiClock
        |
lib/fuji                 host slots (fujiHost) and disk slots (fujiDisk, which embeds DISK_DEVICE)
lib/media/<platform>     disk image formats: one MediaType family per platform, shared IMDImage
lib/printer-emulator     printer, modem, CP/M and speech emulation used by the devices
lib/modem-sniffer, lib/runcpm, lib/sam
        |
lib/FileSystem           FileSystem backends (SD, flash, TNFS, SMB, NFS, FTP, HTTP), FileHandler
lib/network-protocol     NetworkProtocol adapters created by NetworkProtocolFactory from a URL
lib/http                 configuration web UI, REST API and HTTP clients (one ESP and one PC server)
        |
lib/tcpip, lib/TNFSlib, lib/ftp, lib/webdav, lib/fn_esp_http_client, lib/telnet,
lib/fnjson, lib/fnxml, lib/fnhtml, lib/fntext, lib/tinyxml2, lib/encoding, lib/qrcode
        |
lib/hardware             fnSystem, fnWiFi, the IOChannel family (UART, USB ACM, TTY, bus-over-IP),
lib/config               LEDs, buttons, Bluetooth, companion-MCU flashing; fnConfig (fnconfig.ini)
        |
ESP-IDF + components/ + managed_components/    |    POSIX or Win32 + components_pc/
```

## One platform per build

Exactly one `BUILD_*` macro is defined per build: `BUILD_ADAM`, `BUILD_APPLE`, `BUILD_ATARI`,
`BUILD_COCO`, `BUILD_CX16`, `BUILD_H89`, `BUILD_IEC`, `BUILD_LYNX`, `BUILD_MAC`, `BUILD_RC2014`,
`BUILD_RS232` or `BUILD_S100`. It comes from `build_platform` in the board's
`build-platforms/platformio-<board>.ini` on ESP32, or from `FUJINET_TARGET` in `fujinet_pc.cmake`
on the host. A few "switchboard" headers turn that macro into a concrete bus, device set and
media set; everything else is selected by `#ifdef` inside the per-platform files.

| Switchboard | Selects |
|---|---|
| [bus/bus.h](bus/bus.h) | the `systemBus` class: `#include "<bus>/<bus>.h"` for the active platform |
| [device/device.h](device/device.h) | the device headers and the per-platform device globals. It defines objects, so only `src/main.cpp` may include it |
| [device/disk.h](device/disk.h) | `DISK_DEVICE`, the disk class a `fujiDisk` slot embeds |
| [device/printer.h](device/printer.h) | `PRINTER_CLASS`; `device/modem.h`, `device/cassette.h`, `device/netstream.h` and `device/siocpm.h` do the same for those devices |
| [media/media.h](media/media.h) | the platform's `MediaType` headers |

Conventions that follow from this:

- Every bus directory defines a class literally named `systemBus`, its own `virtualDevice` base
  for devices on that bus, and the `FUJI_COMMAND_PACKET` type the shared device bases consume.
  Grep by directory, not by class name.
- `SYSTEM_BUS` is the single bus instance, defined in `src/main.cpp`.
- `theFuji` (a `fujiDevice *`) is set by each `lib/device/<bus>/<bus>Fuji.cpp`; `platformClock`
  and `fnPrinters` are defined the same way per bus.
- Command IDs are one enum in [include/fujiCommandID.h](../include/fujiCommandID.h); values
  overlap across device families, so the target device gives a byte its meaning. Device IDs are
  in [include/fujiDeviceID.h](../include/fujiDeviceID.h), with a separate map for ADAM.
- There is no shared `MediaType` base class. Each `lib/media/<platform>` declares its own.

## Globals

These are the process-wide singletons. CONTRIBUTING.md forbids adding more; hang new state off
the owning device or bus.

| Global | Type | Header | Note |
|---|---|---|---|
| `Config` | `fnConfig` | config/fnConfig.h | the in-memory fnconfig.ini |
| `fnPassword` | `FNPassword` | config/fnPassword.h | web UI password and sessions |
| `fnSystem` | `SystemManager` | hardware/fnSystem.h | reboot, uptime, heap, time, GPIO |
| `fnWiFi` | `WiFiManager` | hardware/fnWiFi.h | `DummyWiFiManager` on the PC build |
| `fnLedManager` | `LedManager` | hardware/led.h | |
| `fnKeyManager` | `KeyManager` | hardware/keys.h | buttons; ESP only |
| `fnSDFAT` | `FileSystemSDFAT` | FileSystem/fnFsSD.h | SD card, or a host directory on PC |
| `fsFlash` | `FileSystemLittleFS` or `FileSystemSPIFFS` | FileSystem/fsFlash.h | picked by `FLASH_LITTLEFS` (ESP) or `FLASH_SPIFFS` (PC) |
| `fnTNFS` | `FileSystemTNFS` | FileSystem/fnFsTNFS.h | |
| `fnHTTPD` | `fnHttpService` | http/httpService.h | the web server |
| `taskMgr` | `fnTaskManager` | task/fnTaskManager.h | PC only |
| `fnClipboard` | `ClipboardManager` | clipboard/clipboardManager.h | |
| `qrManager` | `QRManager` | qrcode/qrmanager.h | |
| `hasher` | `Hash` | encoding/hash.h | |
| `crypto` | `Crypto` | encrypt/crypt.h | WiFi passphrase obfuscation |
| `DISPLAY` | `Display` | display/display.h | only with `ENABLE_DISPLAY` |

## ESP vs PC

`ESP_PLATFORM` is the only firmware-versus-host switch; there is no `FUJINET_PC` macro.

- ESP32: `src/CMakeLists.txt` globs the `*.cpp` files of every directory listed there into every
  target. Platform gating happens inside the files with `#ifdef BUILD_*` or `#ifdef ESP_PLATFORM`,
  so an unguarded file is compiled for all boards. PlatformIO's library dependency finder is off
  (`lib_ldf_mode = off`), so that glob list is the only thing that decides what is built.
- FujiNet-PC: `fujinet_pc.cmake` lists sources explicitly, with a common set plus one block per
  `FUJINET_TARGET` (`ATARI`, `APPLE`, `COCO`, `RS232`, `LYNX`, `ADAM`). Never compiled on PC:
  `console`, `display`, `meatloaf`, `http/webdav`, `libb64`, the `iec`, `mac`, `cx16_i2c`, `h89`,
  `rc2014bus`, `rc2014sio` and `s100spi` buses with their device and media directories, and
  `stuffit` and `fn_esp_http_client` except in unit tests.
- `FileSystem/fnio.h` decides the file API: `fnFile` is the `FileHandler` abstraction for
  `BUILD_ATARI`, `BUILD_APPLE`, `BUILD_COCO`, `BUILD_RS232` and the PC ADAM build, and a plain
  `FILE` everywhere else.

## Directories

| Directory | Role | Origin | ESP | PC |
|---|---|---|---|---|
| [bus/](bus/) | wire protocol per bus, `systemBus`, `DaisyChain` | in-house; `bus/iec/` carries the IECDevice library | all | six buses |
| [clipboard/](clipboard/) | `fnClipboard`, the shared text buffer | in-house | yes | yes |
| [compat/](compat/) | `strlcpy`, `dirent`, `uname`, `termios2` shims | OpenBSD, MIT | guarded | yes |
| [config/](config/) | `fnConfig`, the fnconfig.ini model, device password | in-house | yes | yes |
| [console/](console/) | serial shell behind `ENABLE_CONSOLE` | ESP32Console fork plus cxxopts, improv, uTE | yes | no |
| [device/](device/) | device behaviour per bus and the shared bases | in-house | all | six buses |
| [devrelay/](devrelay/) | SmartPort-over-SLIP request and response types | in-house | empty | APPLE |
| [display/](display/) | LED-strip animation behind `ENABLE_DISPLAY` | Meatloaf | yes | no |
| [encoding/](encoding/) | `Base64`, `Hash` | in-house | yes | yes |
| [encrypt/](encrypt/) | `Crypto` passphrase obfuscation | micro-emacs | yes | yes |
| [FileSystem/](FileSystem/) | `FileSystem` backends and `FileHandler` | in-house | yes | yes |
| [fn_esp_http_client/](fn_esp_http_client/) | forked `esp_http_client` with WebDAV verbs | Espressif, Apache-2.0 | yes | tests |
| [fnjson/](fnjson/), [fnxml/](fnxml/), [fnhtml/](fnhtml/), [fntext/](fntext/) | N: query parsers over fetched documents | in-house | yes | yes |
| [ftp/](ftp/) | `fnFTP` client | in-house | yes | yes |
| [fuji/](fuji/) | `fujiHost` and `fujiDisk` slots | in-house | yes | yes |
| [hardware/](hardware/) | system, WiFi, I/O channels, LEDs, keys, companion MCU | in-house plus Arduino ports | yes | subset |
| [hotsync/](hotsync/) | Palm OS HotSync protocol stack and sync session | palm-sync port, Apache-2.0 | no | tests |
| [http/](http/) | web UI, REST API, HTTP clients; `webdav/` server | in-house; `webdav/` from Meatloaf | yes | yes, not `webdav/` |
| libb64/ | vestigial: holds only a LICENSE file | | no | no |
| [meatloaf/](meatloaf/) | virtual filesystem used by the IEC drive | Meatloaf, GPL-3 | yes | no |
| [media/](media/) | disk image formats | in-house | all | six platforms |
| [modem-sniffer/](modem-sniffer/) | modem traffic dump for the web UI | in-house | yes | yes |
| [network-protocol/](network-protocol/) | N: protocol adapters and their factory | in-house | yes | yes |
| [printer-emulator/](printer-emulator/) | printer models rendering to PDF, SVG, PNG, HTML | in-house | yes | yes |
| [qrcode/](qrcode/) | QR encoder and `QRManager` | `qrcode/qrcode.c` MIT; manager in-house | yes | yes |
| [runcpm/](runcpm/) | CP/M emulator, header-only | RunCPM | via users | via users |
| sam/ | SAM speech synthesis | SAM C port; see its README | yes | yes |
| [stuffit/](stuffit/) | StuffIt, NDIF and BinHex decoders for the Mac | XADMaster LGPL, ndif2raw BSD | yes | tests |
| [task/](task/) | cooperative task manager | in-house | no | yes |
| [tcpip/](tcpip/) | `fnTcpClient`, `fnTcpServer`, `fnUDP`, `fnDNS` | Arduino WiFi ports | yes | yes |
| [telnet/](telnet/) | libtelnet | public domain | yes | yes |
| [tinyxml2/](tinyxml2/) | XML parser | zlib | yes | yes |
| [TNFSlib/](TNFSlib/) | TNFS client | in-house | yes | yes |
| [utils/](utils/) | string, time, URL and misc helpers | in-house plus MIT and LGPL pieces | yes | yes |
| [webdav/](webdav/) | PROPFIND and HTML-index parsers (client side) | in-house | yes | yes |

## Conventions

- [CONTRIBUTING.md](../CONTRIBUTING.md) is the working reference; [docs/cpp-style.md](../docs/cpp-style.md)
  holds the C++ and code-structure rules, with `device/fujiDevice` and `device/NDevice` as the model.
- Two ctests enforce layering: `no_build_ifdefs_in_fujidevice` (no `BUILD_*` conditionals under
  the shared device bases) and `no_system_bus_in_media` (no `SYSTEM_BUS` under `media/`).
