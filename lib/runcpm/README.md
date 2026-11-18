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
PC whichever targets list a CP/M device plus the network adapter. The `runcpm_disk_tests` ctest
builds the disk layer alone, except on Windows (#2431).

## Notes
- The disk layer keeps up to `CPM_OPEN_FILES` host files open between records. A BDOS close,
  delete, rename or make closes one, and the CCP closes the rest before each command, so a file a
  running program holds open can lag on the card until then.
- `globals.h` carries the upstream RunCPM version.
- `abstraction_arduino.h`, `abstraction_posix.h`, `abstraction_vstudio.h`, `posix.h`, `lua.h` and
  `resource.h` are upstream leftovers with no consumer.
