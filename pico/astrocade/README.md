# FujiNet for the Bally Astrocade

An RP2040 cartridge that serves the Astrocade's 8K cart window and, through
it, a FujiNet: browse a TNFS host from the console, boot images over the
network, and let the booted program keep talking to the network. The
protocol is verified end to end in emulation (a MAME cart device runs the
cartridge's own protocol sources against a real fujinet-pc); cartridge
hardware does not exist yet.

## The port, and why everything follows from it

The cartridge edge connector carries **A0-A12, D0-D7, one pre-decoded
Enable** (asserted for reads in 0x2000-0x3FFF), +5V and ground. Nothing
else: no /RD, no /WR, no /IORQ, no clock, no reset. Writes to 0x0000-0x3FFF
are consumed by the console's magic function generator besides. So the cart
is, electrically, a ROM -- and both directions of the mailbox ride the read
path:

- **console -> cart**: reads inside hotspot windows; A0-A7 of the address
  IS the payload byte. This is established Astrocade practice: the 512K
  homebrew mapper switches banks with reads at 0x3F80-0x3FFF, and
  AstroBASIC toggles its tape relay the same way.
- **cart -> console**: bytes core0 paints into the served window. By the
  time core1 sees the read, the byte is ROM as far as it is concerned (the
  O2 "reads are free" rule, used in both directions here).

The window map, register file, interlocks and boot protocol live in
**`firmware/include/fuji_mailbox.h`** -- the single source of truth for the
RP2040 firmware, the MAME device, and (hand-mirrored in
`testrom/fujilib.inc`) the Z80 clients. In one line each:

| Cart offset | Console addr | What |
|---|---|---|
| 0x0000-0x1AFF | 0x2000-0x3AFF | client ROM (6.75K budget) |
| 0x1B00-0x1BFF | 0x3B00 | reply slice (256 of up to 1024 bytes) |
| 0x1C00-0x1C0C | 0x3C00 | status: ACKSEQ, ERR, RXLEN, BOOT_*, magic 'F' 'N', SLICE_ECHO |
| 0x1CFC | 0x3CFC | "FUJI" claim signature (an image carrying it keeps the mailbox after boot) |
| 0x1D00-0x1DFF | 0x3D00 | hotspot: REGSEL (0xFE = armed-only ROM swap) |
| 0x1E00-0x1EFF | 0x3E00 | hotspot: REGDATA (inert without an immediately-preceding REGSEL) |
| 0x1F00-0x1FFF | 0x3F00 | hotspot: TX byte stream |

Stray reads are the hazard on a port like this (Z80 refresh cycles, the
MAME debugger, the OS walking the menu chain). Three independent gates keep
them harmless: REGDATA disarms after one use, a transaction launches only
on a fresh nonzero SEQ, and booting an unclaiming image kills hotspot
decode for the session. Our own clients also run `DI`/polled, so I stays 0
and refresh never lands in cart space at all. `host_test/test_fujimail.c`
fuzzes exactly these cases -- MAME cannot model refresh, so the host test
is where they run.

**Booting** (the part every multicart gets wrong once): the client arms the
swap (`BOOTLOCK` = 0xB5), copies a 6-byte stub to the top of screen RAM --
the only RAM in the machine, and executable -- and jumps to it. The stub
reads the swap hotspot (the cart flips to the staged image between that
read and the next; nothing fetches from cart space meanwhile) and does
`JP 0`. The OS cold-starts, walks the new image's 0x55 sentinel, and the
game is one keypress away. Faithful to a physical cart swap plus RESET.

## Layout

```
build.sh                assemble the Z80 clients (vendored zmac; checkrom.py
                        enforces the fuji_mailbox.h layout on every image)
build-cart.sh           RP2040 firmware (pico-sdk; bakes the CONFIG client in)
run.sh                  run a client in MAME against a live fujinet-pc
emu/                    MAME cart device + apply.sh + SLIP-over-TCP transport
firmware/               RP2040 firmware; include/fuji_mailbox.h is the spec
testrom/                Z80 clients: fujitest, fujiboot, fujicfg + HVGLIB.H
tools/                  vendored zmac (PD, see PROVENANCE.md), mkromh, checkrom
```

The shared protocol sources -- `fujimail.c` (hotspot decode, SEQ/ACKSEQ and
SLICE_ECHO interlocks, DBC push receiver), `fujibus.c` (SLIP + FujiBus
codec, byte-identical with pico/o2 and pico/intellivision), `astromap.c`
(image -> window) -- are hardware-free, tested with plain gcc, and compiled
verbatim into both the firmware and the MAME device. Emulator and cartridge
stay identical by construction, not by discipline.

## Running it

```sh
# 1. Protocol tests, no hardware, no SDK
cd firmware/host_test
gcc -Wall -Wextra -Werror -I.. -I../include -o test_fujibus  test_fujibus.c  ../src/fujibus.c  && ./test_fujibus
gcc -Wall -Wextra -Werror -I../include      -o test_astromap test_astromap.c ../src/astromap.c && ./test_astromap
gcc -Wall -Wextra -Werror -I../include      -o test_fujimail test_fujimail.c ../src/fujimail.c && ./test_fujimail

# 2. Clients
./build.sh                      # fujitest fujiboot fujicfg -> build/*.bin

# 3. The MAME model (a tree with the astrocde BIOS in roms/)
./emu/apply.sh ~/Workspace/mame
make -C ~/Workspace/mame -j$(nproc) NOWERROR=1 REGENIE=1

# 4. Against a live fujinet-pc (its BoIP listener on 127.0.0.1:9995)
./run.sh fujitest               # SSID/version/IP on the console screen
./run.sh fujicfg                # hosts -> browse -> boot over the network

# 5. RP2040 firmware
PICO_SDK_PATH=/usr/share/pico-sdk ./build-cart.sh    # -> fujicade.uf2
```

`run.sh` respects `FUJINET_TCP`, `FUJINET_DEBUG` (per-transaction stderr
log), `FUJINET_BOOTDUMP` (write pushed streams to files, for byte-for-byte
comparison against what was served). `emu/drive.lua` is a headless-run
helper that presses keypad '1' -- the clients register as the first entry
on the OS's own SELECT GAME menu.

The MAME device is selected explicitly (`-cartslot fujinet -cart x.bin`):
size-based autodetection would pick the plain ROM type. An image without
the claim signature runs as a plain ROM through the same device -- that is
the "load anything" path, and commercial dumps behave identically to the
stock cart types.

## Status

Verified in emulation against a real fujinet-pc-rs232 (2026-09):

- **fujitest**: GET_ADAPTERCONFIG_EXTENDED round trip; SSID, firmware
  version and IP rendered on the console.
- **fujiboot**: MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE; 8K DBC
  push byte-identical (`FUJINET_BOOTDUMP` + cmp); swap; the pushed image
  boots off the OS menu and -- because it claims the mailbox -- runs its
  own transaction after booting.
- **fujicfg**: host page (READ_HOST_SLOTS), root directory browse with
  cursor/paging (SET_DIRECTORY_POSITION + READ_DIR_ENTRY, crunched names,
  EOF on the 0x7F marker or a NAKed position), full-length re-read of the
  selection, then the fujiboot path. Root listing only for now, the o2
  CONFIG's proven scope; subdirectory descent is the obvious next step.
- A commercial 8K dump runs standalone through the device, mailbox dead.

NOT YET RUN ON HARDWARE: there is no cartridge board. The EPROM check runs
first: `build/fujitest.bin` is exactly 8192 bytes -- burn it to a 27C64 in
a standard EPROM cartridge and a real console shows FUJINET TEST on the
menu and the NO FUJINET CART diagnostic with the two bytes it read, proving
the header, toolchain, HVGLIB subset and screen/keypad path on real iron
with no link hardware at all.

## Notes and gotchas

- **The sequence rule** (inherited from Intellivision/O2, still true here):
  a client derives its next sequence number from the cart's persisted
  ACKSEQ + 1, never a local counter. Console RESET restarts the client, not
  the cart.
- **SLICE_ECHO is new in this family.** The cart repaints reply slices
  asynchronously and publishes the echo byte last; a client polls it after
  selecting a slice. The o2 protocol lacks this and its emulator model
  admits it cannot catch a client that forgets to poll -- here it can be
  polled at all, and MAME's synchronous transport means the host tests are
  where the async ordering is enforced.
- **One event per Enable assertion.** A ~1us Z80 read spans dozens of
  250MHz polling iterations; core1 records one hotspot event and spins
  until Enable deasserts. (The o2 bus loop lacks this discipline on its
  write strobe and would duplicate TX bytes on real hardware -- worth
  fixing there.)
- **The mailbox stays live until the swap.** Deactivating it when an
  unclaiming image is *staged* (as the o2 firmware port does) drops the
  BOOT_READY publish and strands the client at the progress screen; this
  port deactivates at swap time.
- **EXIT is XINTC (0x02).** An interpreter byte with bit 7 set vectors
  through the user macro table at 0x4FFD, which a fresh client has never
  initialized; the crash lands in whatever garbage is there. Cost a
  debugging session; confirmed against interpreter streams in commercial
  dumps (280 Zzzap, Bally Pin).
- **HVGLIB.H here is a self-authored subset** transcribed from the Nutting
  manual (system-call indexes, ports, ROM cells, SENTRY codes) -- checked
  in for provenance-clean CI, named so sources read like every other
  BallyAlley-convention program.

## The eventual hardware

A plain Pico covers the port with room to spare: A0-A12 on GP0-12, /ENABLE
on GP13, D0-D7 on GP14-21, four pins free. The board file is
`firmware/boards/fujicade.{cmake,h}`; a level-shifted PCB gets a sibling
board file. Before committing a PCB:

- 5V TTL bus into a 3.3V RP2040 needs real level shifting (74LVC245 on
  data with Enable as DIR/OE; LVC-class inputs for address and Enable).
- Console power sense: cart pin 20 (+5V) through a divider into GP26, so
  USB-powered bench operation (the ESP32-S3 host powers the cart first)
  can be told apart from a live console -- the O2/Intellivision lesson.
- Enable polarity and timing, and whether it pulses during Z80 refresh or
  magic writes, want one scope session on a real console; the protocol
  tolerates all outcomes but the bus loop's tri-state discipline should be
  confirmed, not assumed.
- The ESP32-S3 side is `build-platforms/platformio-fujiversal-astrocade.ini`
  plus `include/pinmap/fujiversal-astrocade.h` -- console-agnostic, USB CDC
  host, VID filter 0xCafe.
