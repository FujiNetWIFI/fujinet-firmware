# lib/device/drivewire

The CoCo and Dragon device set for `BUILD_COCO`.

## How it fits
- The bus in [lib/bus/drivewire/](../../bus/drivewire/) dispatches by opcode, so the disk, printer,
  modem and cassette derive its `drivewireDevice` base directly; the Fuji, network, clock and CP/M
  devices derive `virtualDevice` and answer `processCommand()`. Images come from
  [lib/media/drivewire/](../../media/drivewire/).
- `src/main.cpp` hands the printer to the bus with `setPrinter()`; the bus keeps no daisy chain.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_COCO`. PC: `FUJINET_TARGET=COCO`
lists the Fuji, disk, printer, clock and network files; the modem, cassette and CP/M devices are
ESP only.

## Notes
- The bus creates a `drivewireNetwork` per unit on first use.
