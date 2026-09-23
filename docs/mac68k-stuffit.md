# StuffIt archives on the Mac 68k FujiNet

A `.sit`, `.sea`, `.hqx` or `.bin` (MacBinary) archive can be mounted in
any Mac slot. At mount time the ESP32 picks the disk image inside it, unpacks
it into PSRAM and mounts that: a 400K/800K raw or DiskCopy 4.2 image as the
floppy (slot 5), anything else that is HFS as an HD20 (slots 1-4). Disk Copy 6
(NDIF) images are decoded to a raw volume first. The unpacked image can be
downloaded from `/sitdownload?deviceslot=N`.

Supported: classic `SIT!` and StuffIt 5 archives, methods 0 (store), 1 (RLE),
2 (LZW), 3 (Huffman), 13 (LZ+Huffman) and 15 (Arsenic), wrapped in BinHex 4.0
or MacBinary or not. Not supported: StuffIt X, encrypted entries, methods 5
and 14, Compact Pro.

The image lives in PSRAM (up to 3 MB) and is gone on unmount; writes by the
Mac are not saved back to the archive. Arsenic needs about 2.6 MB of scratch
while unpacking, and an 800K floppy uses 1.2 MB of GCR tracks, so mount a
large archive before the floppy.

`lib/stuffit/` is plain C99 with a caller-supplied allocator. The decoders are
ported from The Unarchiver (LGPL) and the NDIF decoder from ndif2raw (BSD).
`tests/unsit.c` is a host CLI over it; `tests/stuffit_test.sh` compares its
output with `unar` over a directory of archives (`tests/corpus_manifest.txt`
lists the 44 used during development) and `tests/ndif_test.sh` compares NDIF
decoding with ndif2raw.
