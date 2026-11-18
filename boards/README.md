# boards

PlatformIO board definitions for FujiNet hardware, plus two JSON files that only
[mcuconfig/](../mcuconfig/) reads.

## How it fits
- A board ini in [build-platforms/](../build-platforms/) names one of these in its `board` key;
  PlatformIO finds them here because `boards/` is its default custom-board directory.
- Each JSON sets the flash size and points at one of the root `fujinet_partitions_*.csv` tables.
- `buses.json` and `capabilities.json` are read by `mcuconfig/mcuconfig.py` to validate pinmaps.
  Nothing in the firmware or the PlatformIO build reads them.

## Build
Consumed by every ESP32 build through the generated `platformio-generated.ini`. The PC build does
not use this directory.

## Notes
`fujinet-v1-4mb.json`, `fujinet-esp32s3.json` and `fujinet-esp32s3-8mb.json` are not selected by
any board ini.
