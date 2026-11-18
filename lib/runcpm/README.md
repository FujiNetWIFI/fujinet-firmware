# lib/runcpm

The RunCPM Z80 CP/M 2.2 emulator (MockbaTheBorg), header-only, embedded so a computer can run CP/M
through its FujiNet.

## How it fits
- Each consumer `.cpp` includes exactly one abstraction header: the per-bus `cpm` devices under
  [lib/device/](../device/) and `lib/network-protocol/CPM.cpp`.
- Those headers share `abstraction_fujinet_disk.h` for disk access: CP/M drives are folders under
  `/CPM/` on `fnSDFAT` from [lib/FileSystem/](../FileSystem/). The CCP binary name comes from the
  `[CPM]` config section.

## Build
No sources of its own, so it reaches a build only through its consumers: every ESP target, and on
PC whichever targets list a CP/M device plus the network adapter.

## Notes
- `globals.h` carries the upstream RunCPM version.
- `abstraction_arduino.h`, `abstraction_posix.h`, `abstraction_vstudio.h`, `posix.h`, `lua.h` and
  `resource.h` are upstream leftovers with no consumer.
