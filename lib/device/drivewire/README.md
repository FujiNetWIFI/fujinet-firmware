# lib/device/drivewire

The CoCo and Dragon device set for `BUILD_COCO`.

## How it fits
- The bus in [lib/bus/drivewire/](../../bus/drivewire/) dispatches by opcode, so the disk, printer,
  modem and cassette derive its `drivewireDevice` base directly; the Fuji, network, clock and CP/M
  devices derive `virtualDevice` and answer `processCommand()`. Disk images are the CoCo, Dragon
  and PMD 85 formats in [lib/media/disk/](../../media/disk/); cartridges are `CoCoCartROM` in
  [lib/media/rom/](../../media/rom/).
- `drivewireFuji` overrides `openROM()` with the cartridge formats in `CoCoROMFormats.h`, and
  `mount_media()` to pass the host to the disk.
- `src/main.cpp` hands the printer to the bus with `setPrinter()`; the bus keeps no daisy chain.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_COCO`. PC: `FUJINET_TARGET=COCO`
lists the Fuji, disk, printer, clock and network files; the modem, cassette and CP/M devices are
ESP only.

## Notes
- The bus creates a `drivewireNetwork` per unit on first use.
