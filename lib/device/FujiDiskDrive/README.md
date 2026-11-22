# lib/device/FujiDiskDrive

The shared base for every bus's disk drive, and the contract code outside the drive uses to ask
about it.

## How it works

- `isMounted()` is the authority on whether the drive has an image mounted. Ask the drive, not a
  `fujiDisk` slot or a cached file handle.
- A drive owns its mounted image as a `unique_ptr` in `_media`, the platform's `MediaType` from
  [lib/media/](../../media/) (`DiskDriveMedia`), and resets it once nothing is mounted. `iecDrive`
  holds no `MediaType`, so it overrides `isMounted()`.
- `FujiDiskDrive` derives from `DiskDriveBase`, which each bus header in [lib/bus/](../../bus/)
  names: `virtualDevice` on most buses, `drivewireDevice` on DriveWire, `IECFileDevice` on IEC.
  Its constructors are the base's.

Subclasses: `sioDisk`, `iwmDisk` (and `iwmDisk2`), `drivewireDisk`, `rs232Disk`, `adamDisk`,
`lynxDisk`, `iecDrive`, `macFloppy`, `cx16Disk`, `rc2014Disk`, and `s100spiDisk`, each in its bus
directory under [lib/device/](../).

## How it fits

- Every `DISK_DEVICE` selected by `lib/device/disk.h` is a `FujiDiskDrive`.
- Covered by the `no_build_ifdefs_in_fujidevice` ctest.

## Build

Header only; reaches every ESP and PC target through the per-bus disk headers that include it.
