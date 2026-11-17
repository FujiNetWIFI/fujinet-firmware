# lib/stuffit

Pure C99 decoders for classic Macintosh archives, so the Mac target can mount `.sit`, `.hqx` and
Disk Copy images without unpacking them elsewhere first.

## How it fits
- The single consumer is `sitMount` in [lib/media/mac/](../media/mac/), which unpacks an archive
  into a mountable image. `docs/mac68k-stuffit.md` describes the supported formats and the test
  corpus.

## Build
ESP: globbed into every target but only reached from the Mac media code. PC: not part of the
firmware build; `tests/CMakeLists.txt` builds the `unsit` CLI and the NDIF block test from these
sources.

## Notes
- `stuffit.c`, the `sit_*` files and `binhex.c` are ported from The Unarchiver's
  XADMaster (LGPL-2.1-or-later); `stuffit.h` is original.
- `ndif.c` is from ndif2raw (BSD-3-Clause).
