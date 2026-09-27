# Writable floppies on the Mac 68k FujiNet

Status: **working** (2026-09-16: a 512Ke boots from a 400K MFS image and
the Finder's writes land in the image; every captured sector decoded with
a good checksum on the first try). Sector
images (`.dsk`, `.img`, DiskCopy 4.2) mounted read/write in slot 5 can be
written by the Mac; MOOF images stay read-only. This note explains how
the write path works, what to test, and what can go wrong.

## How the Mac writes

The Sony driver never rewrites a whole sector slot. To write a sector it
seeks, reads address fields until the target sector's field passes, then
raises write request and streams: a few self-sync nibbles, the data mark
`D5 AA AD`, the sector number nibble, 703 GCR nibbles (12 tag + 512 data
bytes, Sony-scrambled and checksummed), and the epilogue `DE AA FF`.
Formatting ("Erase Disk") writes whole tracks, address fields included.

The IWM puts this on the WR line as flux transitions: one toggle per
1 bit, nothing for a 0, 2 µs per bit cell. Because every GCR nibble
starts with a 1 and the self-sync nibbles are `FF` plus two zero bits,
the line never goes more than three cells without a transition while
the Mac is writing, and it is static otherwise.

## Pico: capture (`pico/mac/commands.c`, `pico/mac/gcr_capture.pio`)

* WR is the Pico's `MCI_WR` (GPIO 15), the same pin the HD20 protocol
  receives on. PIO0 has no room for another program, so in floppy mode the
  HD20 receive program is swapped out for `gcr_capture` on the same state
  machine (`capture_start()` / `capture_stop()` in `switch_to_floppy()` /
  `switch_to_dcd()`).
* `gcr_capture` polls WR at 31.25 MHz. A window of twenty 3-cycle polls is
  one 2 µs cell: an edge inside it is a 1 and re-centres the next window
  half a cell later; no edge is a 0. Bits are shifted in MSB first and
  pushed every 8 bits, and a DMA channel drains them into a 4 KB ring
  (`cap_ring`) so the main loop never has to keep up with the FIFO.
* `capture_service()` (called from `floppy_loop()`) scans the ring. Idle
  line = `0x00` bytes. The first non-zero byte opens a frame; bytes are
  forwarded to the ESP32 in `'w' <len> <bytes>` chunks of up to 64 bytes;
  four consecutive `0x00` bytes (32 idle cells, 64 µs) close the frame
  with `'w' 0`. Nothing is forwarded unless the ESP32 has said the disk is
  writable (`'u'`) and a disk is in.
* Write protect: the `!WRTPRT` status line follows `'u'` (unprotected) /
  `'l'` (locked) from the ESP32; insert, eject and web-eject reset it to
  locked. The ESP32 sends `'u'` only for a sector image mounted with
  write access.

## ESP32: decode and store

* `systemBus::handle_write_frame()` (`lib/bus/mac/mac.cpp`) reassembles
  the frames. The stream runs at 62.5 KB/s and the UART buffer is 2 KB, so
  it keeps reading until the end marker (or a 200 ms stall) instead of
  returning to the main loop between chunks. The RMT read stream is fed
  from an interrupt and is not affected.
* `macFloppy::write_capture_data()/write_capture_end()`
  (`lib/device/mac/floppy.cpp`) collect the bits in a 16 KB PSRAM buffer
  (a whole track is at most 9.3 KB), note which head SEL selected when the
  write began, and on the end marker run `mac_gcr_decode_capture()`.
* `mac_gcr_decode_capture()` (`lib/media/mac/macGCR.cpp`) searches the
  bitstream for `D5 AA 96` / `D5 AA AD` at every bit offset, decodes the
  sector number and the 703 nibbles, verifies the Sony checksum and
  reports each data field; a data field right after an address field with
  the same sector number carries that field's cylinder and side (a
  format). Otherwise the cylinder is the current head position.
* `MediaTypeFloppyImage::write_sector()` writes the 512 data bytes (and
  the 12 tag bytes if the image has tags) into the image file at the
  block's offset, flushes, and patches the sector's 703 nibbles into the
  encoded track in PSRAM with `mac_gcr_patch_sector()`. Tracks are byte
  aligned and laid out deterministically, so the data field of logical
  sector *n* is at a fixed byte offset (`mac_gcr_sector_data_offset()`).
* `macFloppy::reload_track_buffers()` then copies both sides of the
  current cylinder into the RMT double buffers, so the Mac's write-verify
  pass on the next revolution reads the new data.

All of the pure C++ parts are covered by `tests/mac_gcr_test.cpp`:
captured data fields at all 16 bit alignments with and without an address
field, a corrupted capture, and an in-place patch followed by a full
track decode.

## Test plan

1. Mount a scratch copy of an 800K sector image in slot 5 with mode 2
   (read/write), Mac off, power on and boot from BlueSCSI. The Finder
   should show the floppy without the lock icon (the Pico prints
   "Disk writable").
2. Copy a small file onto the floppy. Expected on the consoles: Pico
   "Floppy write forwarded" per sector; ESP32 "Floppy write: N bytes
   captured at cyl C side H" then "wrote C.. H.. S.. (block ..)". The
   Finder must not report a write error (the driver verifies each sector
   on the next revolution).
3. Eject, re-mount, check the file is there and opens. Then check the
   image on the TNFS server with a host tool (e.g. `hfsutils`).
4. Erase Disk from the Finder: whole-track writes; every sector of every
   track should be reported with cylinder/side from the address fields.
5. HD20 slots still work after the floppy has been used (the PIO program
   swap back to the HD20 receiver in `capture_stop()`).

## Things that may need adjusting

* **Cell timing** in `gcr_capture.pio`: 20 polls × 3 cycles at 31.25 MHz
  = 1.92 µs plus loop overhead ≈ 2.0 µs. If sectors decode with bad
  checksums, the window is off; the `set x, 19` count and the `nop [28]`
  half-cell delay are the knobs. The `t` status trace on the Pico console
  shows the Mac's commands around the write.
* **Throughput**: a Finder copy writes one sector per revolution, which is
  trivial. A format writes ~9.3 KB per track back to back; if the ESP32
  logs "overflow" or "no data field found", the UART buffer overran.
  Raising `uart_buffer_size` in `lib/hardware/ESP32UARTChannel.cpp` for
  the Mac build, or sending IWM nibbles instead of raw bits from the
  Pico, are the options.
* **TNFS latency**: each written sector costs a seek + 512-byte write +
  flush over TNFS before the track is patched. If the Mac's verify read
  beats it, the Finder retries; if it keeps failing, patch the track
  first and write the file afterwards.
* **Spurious frames**: noise on WR while idle would produce frames with
  no marks in them; they are logged and ignored.
* **Both halves must be updated together**: an old Pico never sends `'w'`
  frames and ignores `'u'`/`'l'`; an old ESP32 would treat `'w'` as an
  unknown HD20 command.
