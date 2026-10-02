# lib/device/fujiDevice

The shared FujiNet CONFIG device: the one every platform's `<bus>Fuji` class derives from and the
global `theFuji` points at. It owns the host and disk slots, WiFi configuration, directory
browsing and image mounting, and it is the model for how device code should be structured.

## Layout

| File | Defines |
|---|---|
| `fujiDevice.h`, `fujiDevice.cpp` | `fujiDevice`, `FujiDeviceChain`, the `FujiPacketLike` concept, `disk_slot`, `dirEntryDetails`, and the `handlers` table of `FUJI_*` commands |
| `FujiDeviceMixin.h` | `FujiDeviceMixin`: a base whose `processCommand()` looks the command up in the map returned by `commandHandlers()`; `FM_CMD_HANDLER` builds a map entry |
| `Base64Mixin.h`, `.cpp` | `FUJI_BASE64_ENCODE_*` and `FUJI_BASE64_DECODE_*` over `Base64` from [lib/encoding/](../../encoding/) |
| `HashMixin.h`, `.cpp` | `FUJI_HASH_*` over `Hash` from [lib/encoding/](../../encoding/) |
| `QRMixin.h`, `.cpp` | `FUJI_QRCODE_*` over `QRManager` from [lib/qrcode/](../../qrcode/); `qr_encode()` is virtual so a platform can unpack its parameters differently |
| `AppKeyMixin.h`, `.cpp` | `FUJI_OPEN_APPKEY`, `FUJI_CLOSE_APPKEY`, `FUJI_READ_APPKEY`, `FUJI_WRITE_APPKEY`; `appkey_read()` and `appkey_write()` are the platform hooks |

## How it works

- `fujiDevice` is `FujiDeviceChain<Base64Mixin, HashMixin, QRMixin, AppKeyMixin>` plus a
  `std::function` table keyed by `fujiCommandID_t`. `processCommand()` offers the packet to each
  mixin in turn, then to the table, and returns false for an unknown command so the per-bus
  subclass can handle its own extras.
- A new self-contained feature is a new mixin added to the chain; a new command is one table entry
  and one short handler. Neither means growing a switch.
- Handlers come in three layers: `fujicmd_*` faces the bus (accept the transaction, call the core,
  reply), `fujicore_*` is pure logic with no bus access, and `fujidev_*` are per-packet hooks a
  subclass overrides.
- Slots: `_fnHosts` and `_fnDisks` hold the `fujiHost` and `fujiDisk` objects from
  [lib/fuji/](../../fuji/); `populate_slots_from_config()` and `populate_config_from_slots()` keep
  them in step with `Config`. Mounting an image opens the file through the host, then calls
  `mount_media()`, which hands the handle to `DISK_DEVICE::mount()`.

## Subclass contract

| Virtual | Required | Purpose |
|---|---|---|
| `setup()` | yes | create and register the platform's devices on `SYSTEM_BUS` |
| `set_additional_direntry_details()` | yes | append the platform's directory-entry fields |
| `mount_media()`, `insert_boot_device()`, `get_disk_dev()` | no | platform disk handling |
| `announce_rotation()`, `fujidev_set_device_fullpath()`, `fujidev_copy_file()` | no | per-packet hooks |
| `qr_encode()`, `appkey_read()`, `appkey_write()` | no | mixin hooks |

Subclasses: `sioFuji`, `iwmFuji`, `drivewireFuji`, `rs232Fuji`, `adamFuji`, `lynxFuji`, `iecFuji`,
`macFuji` and the legacy `rc2014Fuji`, each in its bus directory under [lib/device/](../).

## How it fits

- The per-bus `virtualDevice` entry point calls `processCommand()` here first; commands it does
  not recognise fall through to the subclass.
- Depends on [lib/fuji/](../../fuji/), [lib/config/](../../config/), [lib/FileSystem/](../../FileSystem/)
  and [lib/hardware/](../../hardware/) for WiFi scanning.
- The `no_build_ifdefs_in_fujidevice` ctest rejects any `BUILD_*` conditional in this directory.

## Build

Compiled into every ESP target and every PC target. Because it is unguarded, it needs the active
bus to define `FUJI_COMMAND_PACKET`; the `static_assert` on `FujiPacketLike` is the first thing
that fails on a bus that does not.
