# lib/stuffit

Pure C99 decoders for classic Macintosh archives, so the Mac target can mount `.sit`, `.hqx` and
Disk Copy images without unpacking them elsewhere first.

## Layout
| File | Defines |
|---|---|
| `stuffit.h`, `stuffit.c` | the `sit_open` / `sit_next_entry` / `sit_extract` API for both the classic SIT and StuffIt 5 formats, with a caller-supplied `sit_allocator`; the header is original, the parser is ported from The Unarchiver's XADMaster (LGPL-2.1-or-later) |
| `sit_rle90.c`, `sit_lzw.c`, `sit_huffman.c`, `sit_lzh13.c`, `sit_arsenic.c` (with `sit_bwt.c`), `sit_prefixcode.c`, `sit_crc16.c`, `sit_io.c`, `sit_bitreader.h`, `sit_internal.h` | one decompressor per StuffIt method plus the shared bit reader, prefix-code tree, CRC and byte source; all ported from XADMaster |
| `ndif.h`, `ndif.c` | Disk Copy 6 NDIF images (`bcem` chunk table, ADC and KenCode), streamed whole or read a few blocks at a time; from ndif2raw (BSD-3-Clause) |
| `binhex.h`, `binhex.c` | BinHex 4.0 unwrapping on top of the RLE90 reader; from XADMaster |

## How it fits
- The single consumer is `sitMount` in [lib/media/mac/](../media/mac/), which unpacks an archive
  into a mountable image. `docs/mac68k-stuffit.md` describes the supported formats and the test
  corpus.

## Build
ESP: globbed into every target but only reached from the Mac media code. PC: not part of the
firmware build; `tests/CMakeLists.txt` builds the `unsit` CLI and the NDIF block test from these
sources.
