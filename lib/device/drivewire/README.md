# lib/device/drivewire

The CoCo and Dragon device set for `BUILD_COCO`.

## How it fits
- The bus in [lib/bus/drivewire/](../../bus/drivewire/) dispatches by opcode, so the disk, printer,
  modem and cassette derive its `drivewireDevice` base directly; the Fuji, network, clock and CP/M
  devices derive `virtualDevice` and answer `processCommand()`. Disk images are the CoCo and
  Dragon formats in [lib/media/disk/](../../media/disk/); cartridges (`CoCoCartROM`) and PMD 85 ROM
  modules (`PMD85ROM`) are in [lib/media/rom/](../../media/rom/).
- `drivewireFuji` overrides `openROM()` with the cartridge formats in `CoCoROMFormats.h`, and
  `mount_media()` to hold a PMD 85 ROM module or pass the host to the disk.
- `src/main.cpp` hands the printer to the bus with `setPrinter()`; the bus keeps no daisy chain.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_COCO`. PC: `FUJINET_TARGET=COCO`
lists the Fuji, disk, ROM module, printer, clock and network files; the modem, cassette and CP/M
devices are ESP only.

## Notes
- The bus creates a `drivewireNetwork` per unit on first use.
- A PMD 85 ROM module mounts in a drive slot but is held by `drivewireFuji`, not the slot's
  `drivewireDisk`; the bus asks the slot for a module before the disk when it answers `OP_READEX`.
  The file only reads forward, so a page before the last one read reopens it.
