# lib/device/comlynx

The Atari Lynx device set for `BUILD_LYNX`.

## Layout
| File | Defines |
|---|---|
| `lynxFuji.h`, `lynxFuji.cpp` | `lynxFuji : fujiDevice` (`.lnx` images); defines `platformFuji` and sets `theFuji`; owns the `lynxNetStream`; `setup()` registers itself, the disks, the networks and the stream device |
| `disk.h`, `disk.cpp` | `lynxDisk`, the `DISK_DEVICE`; sends blocks LZ4-compressed |
| `lynxNetwork.h`, `lynxNetwork.cpp` | `lynxNetwork : NDevice` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `lynxPrinter` and `printerlist`; defines `fnPrinters` |
| `netstream.h`, `netstream.cpp` | `lynxNetStream`, UDP MIDI streaming |
| `redeye.h`, `redeye.cpp` | the RedEye multiplayer tables (`GAME_STATE_T`, `GAME_T`, `GAME_LIST_T`) and relay logic the bus switches on |
| `modem.h-na`, `modem.cpp-na` | a modem device disabled by the `-na` suffix |

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/comlynx/](../../bus/comlynx/) and overrides
  `comlynx_process()`. Images come from [lib/media/lynx/](../../media/lynx/).
- `lynxFuji` overrides `setup()`, `shutdown()`, `set_additional_direntry_details()` and
  `comlynx_process()`. `lynxNetwork` overrides `fujidev_read()`. There is no clock device.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_LYNX`. PC: `FUJINET_TARGET=LYNX`
lists every file and adds `components/lz4` for the disk device.
