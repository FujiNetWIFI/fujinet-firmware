# lib/runcpm

The RunCPM Z80 CP/M 2.2 emulator (MockbaTheBorg), header-only, embedded so a computer can run CP/M
through its FujiNet.

## Layout
| File | Defines |
|---|---|
| `globals.h`, `cpu.h`, `cpm.h`, `ccp.h`, `disk.h`, `ram.h`, `console.h`, `host.h` | the emulator; `globals.h` carries the RunCPM version and the `RUNCPM_STATIC_IMPL` switch that makes every symbol static |
| `abstraction_fujinet.h` | console and disk I/O for the SIO, RS232 and RC2014 CP/M devices |
| `abstraction_fujinet_apple2.h` | the queue-based variant used by the IWM and DriveWire CP/M devices |
| `abstraction_network_protocol.h` | the static variant behind the `CPM:` network adapter |
| `abstraction_arduino.h`, `abstraction_posix.h`, `abstraction_vstudio.h`, `posix.h`, `lua.h`, `resource.h` | upstream leftovers with no consumer |

## How it fits
- Each consumer `.cpp` includes exactly one abstraction header: the per-bus `cpm` devices under
  [lib/device/](../device/) and `lib/network-protocol/CPM.cpp`.
- Disk access is `fnSDFAT` from [lib/FileSystem/](../FileSystem/); the CCP binary name comes from
  the `[CPM]` config section.

## Build
No sources of its own, so it reaches a build only through its consumers: every ESP target, and on
PC whichever targets list a CP/M device plus the network adapter.
