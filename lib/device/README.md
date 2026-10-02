# lib/device

Device behaviour, one directory per bus plus the shared bases every per-bus device derives from. A
device decides what a command means (mount this image, read that sector, open this URL); the bus
in [lib/bus/](../bus/) decides how bytes move, and [lib/media/](../media/) decodes image files.

## Switchboards

| File | Selects |
|---|---|
| `device.h` | the device headers for the active `BUILD_*` and the per-platform device globals (`sioR`, `sioZ`, `streamDev`, ...). It defines objects, so only `src/main.cpp` includes it |
| `disk.h` | `DISK_DEVICE`, the disk class embedded in every `fujiDisk` slot, and `disk_access_flags_t` |
| `printer.h` | `PRINTER_CLASS` |
| `modem.h`, `cassette.h`, `netstream.h`, `siocpm.h` | the matching device header per platform; `siocpm.h` is the CP/M switch for all platforms despite its name |

## Shared bases

The three directories below are the model the per-bus code is being moved toward; read them before
writing device code (see [docs/cpp-style.md](../../docs/cpp-style.md)). A ctest fails any `BUILD_*`
conditional inside them: platform differences are expressed by overriding a virtual in the per-bus
subclass, never by branching here.

| Directory | Class | Role |
|---|---|---|
| [fujiDevice/](fujiDevice/) | `fujiDevice` | the FujiNet CONFIG device: host and disk slots, WiFi, directories, mounting. Built from `FujiDeviceChain<Base64Mixin, HashMixin, QRMixin, AppKeyMixin>`, each mixin owning one command family, plus a table of `FUJI_*` handlers |
| [NDevice/](NDevice/) | `NDevice` | the N: network device: `NET_*` commands dispatched through a table, a `NetworkProtocol` chosen from the URL scheme by `NetworkProtocolFactory`, and an `NParser` strategy (`JSONParser`, `XMLParser`, `HTMLParser`) for queries |
| [fujiClock/](fujiClock/) | `fujiClock` | the APETime-compatible clock: `APETIME_*` commands and the time formatters |

Each base inherits `virtualDevice` virtually, so a subclass such as `sioFuji` is one object that is
both the bus's `virtualDevice` and the shared base. The subclass implements the bus entry point
(`sio_process`, `iwm_ctrl`, `processCommand`, ...) and normally calls the base's
`processCommand()` first, then handles anything platform-specific.

## Naming

Every per-bus directory follows one layout:

| File | Defines |
|---|---|
| `<bus>Fuji.h`, `.cpp` | the `fujiDevice` subclass; its `.cpp` defines `platformFuji` and points the global `theFuji` at it |
| `disk.h`, `.cpp` | the `DISK_DEVICE` class |
| `<bus>Network.h`, `.cpp` | the `NDevice` subclass |
| `<bus>Clock.h`, `.cpp` | the `fujiClock` subclass; defines the global `platformClock` |
| `printer.*`, `printerlist.*` | the printer device and the global `fnPrinters` |
| `modem.*`, `cpm.*`, `cassette.*`, `netstream.*` | the remaining devices a platform offers |

## Per-bus directories

| Directory | Platform | Devices | ESP | PC | Status |
|---|---|---|---|---|---|
| [sio/](sio/) | `BUILD_ATARI` | fuji, disk, network, clock, printer, modem, cassette, CP/M, voice, PCLink, MIDI stream | yes | yes | |
| [iwm/](iwm/) | `BUILD_APPLE` | fuji, SmartPort disk, Disk II, network, clock, printer, modem, CP/M | yes | yes | |
| [drivewire/](drivewire/) | `BUILD_COCO` | fuji, disk, network, clock, printer, modem, cassette, CP/M | yes | yes | modem, cassette and CP/M are ESP only |
| [rs232/](rs232/) | `BUILD_RS232` | fuji, disk, network, clock, printer, modem, CP/M | yes | yes | also every fujiversal board |
| [adamnet/](adamnet/) | `BUILD_ADAM` | fuji, disk, network, clock, printer, keyboard | yes | yes | no modem |
| [comlynx/](comlynx/) | `BUILD_LYNX` | fuji, disk, network, printer, net stream, RedEye | yes | yes | modem disabled |
| [iec/](iec/) | `BUILD_IEC` | fuji, drive, network, clock, printer, modem | yes | no | drive, clock, printer and modem derive from the IECDevice library directly |
| [mac/](mac/) | `BUILD_MAC` | fuji, floppy (also HD20), printer, modem | yes | no | |
| [cx16_i2c/](cx16_i2c/) | `BUILD_CX16` | fuji, disk, printer, modem | fails | no | legacy, not on the shared bases (#1658) |
| [h89/](h89/) | `BUILD_H89` | fuji, disk, network, printer, modem | fails | no | legacy (#1658) |
| [rc2014/](rc2014/) | `BUILD_RC2014` | fuji, disk, network, printer, modem, CP/M | fails | no | legacy (#1658); the bus lives in `lib/bus/rc2014bus/` |
| [s100spi/](s100spi/) | `BUILD_S100` | fuji, disk, network, printer, modem | fails | no | legacy (#1658) |

## How it fits

- Devices are registered on `SYSTEM_BUS` by `main_setup()` in [src/](../../src/) and by each
  platform's `<bus>Fuji::setup()`, using the IDs in [include/fujiDeviceID.h](../../include/fujiDeviceID.h).
- `fujiDevice` owns the `fujiHost` and `fujiDisk` slots from [lib/fuji/](../fuji/); every
  `DISK_DEVICE` instance lives inside a `fujiDisk`.
- Printers render through [lib/printer-emulator/](../printer-emulator/), modems log through
  [lib/modem-sniffer/](../modem-sniffer/), CP/M devices embed [lib/runcpm/](../runcpm/), and
  the network devices open [lib/network-protocol/](../network-protocol/) adapters.

## Build

ESP: `src/CMakeLists.txt` globs every directory here into every target; the per-bus files guard
themselves with `#ifdef BUILD_*`, while the shared bases are unguarded and compile for all boards.
PC: `fujinet_pc.cmake` compiles the shared bases for every `FUJINET_TARGET` and adds one per-bus
directory per target.
