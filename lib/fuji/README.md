# lib/fuji

The host-slot and disk-slot model the CONFIG program manipulates: a host slot binds a name to a
`FileSystem`, and a disk slot binds a mounted image to a `DISK_DEVICE`.

## How it fits
- `fujiDevice` in [lib/device/fujiDevice/](../device/fujiDevice/) owns the `_fnHosts` and
  `_fnDisks` arrays, so every disk device instance lives inside a `fujiDisk`. It syncs them with
  `Config` in `populate_slots_from_config()` and `populate_config_from_slots()`.
- Mounting an image runs `fujicore_mount_disk_image_success()`: `fujiHost::fnfile_open()`, then
  `mount_media()`, then `DISK_DEVICE::mount()`, which picks a `MediaType` by file extension and
  owns the handle from then on; the slot keeps no handle.
- Backends come from [lib/FileSystem/](../FileSystem/). `fujiDisk.h` includes
  [lib/device/disk.h](../device/disk.h), so this directory depends on the build's disk class.

## Build
ESP: globbed by `src/CMakeLists.txt` into every target. PC: both files are listed in
`fujinet_pc.cmake` for every target.
