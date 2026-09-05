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
| 0x1D00-0x1DFF | 0x3D00 | hotspot: REGSEL (0x80+page = APPBANK low-half select; 0xF0-0xFF reserved, 0xFE = armed-only ROM swap) |
| 0x1E00-0x1EFF | 0x3E00 | hotspot: REGDATA (inert without an immediately-preceding REGSEL) |
| 0x1F00-0x1FFF | 0x3F00 | hotspot: TX byte stream (and, mailbox-dead only, the game mapper's bank selects) |

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

## Banked images (protocol v2)

Two schemes cover carts bigger than the 8K window, mutually exclusive by
construction (spec prose in `fuji_mailbox.h`, mapping authority in
`astromap.c`):

- **GAME** -- an exact 256K/512K image with no claim boots onto the
  established homebrew mapper, byte-compatible with MAME's
  `rom_256k`/`rom_512k`: 0x2000-0x2FFF fixed to the LAST 4K bank, a
  switched 4K bank above it, and a read at 0x3FC0-0x3FFF (256K) /
  0x3F80-0x3FFF (512K) that selects `addr & mask` AND returns the bank
  number as the data byte. Those hotspots sit inside the TX page, which is
  why game banking exists only with the mailbox dead -- and why the mailbox
  pages never moved: every ported app streams server-chosen bytes across
  the whole TX page.
- **APPBANK** -- a claimed image of 8K + k*4K keeps the mailbox fully
  live. The image is flat 4K pages; page 0 boots at 0x2000-0x2FFF (the LOW
  half is what banks), page 1 is the fixed high half, always served from
  the RAM window so repaints stay visible. One read at 0x3D80+page switches
  instantly (core1 handles it inline, like the swap). Console RESET cannot
  restore page 0, so every page opens with a stamped sentinel header whose
  start vector re-selects page 0 from high-half code -- `tools/mkbanked.py`
  stamps it, reset entry 0x3000 by convention.

Storage is tiered (`fuji_store.c`): the classic 8K stage, a 128K RAM store,
and the top 512K of flash (erased and programmed on core0 while core1 keeps
serving from SRAM; `FN_BOOT_ERR_STOREBUSY` when the only fitting store is
the one being served from). The serve loop itself is a bank-pointer pair --
`bank[a >> 12][a & 0xFFF]` -- so the flat path is byte-identical to v1.

## Layout

```
build.sh                assemble the Z80 clients (vendored zmac; checkrom.py
                        enforces the fuji_mailbox.h layout on every image;
                        fujibank/gamebank build banked images via the packers)
build-cart.sh           RP2040 firmware (pico-sdk; bakes the CONFIG client in --
                        the real fujinet-config build when build/config.bin
                        is present, testrom stand-ins otherwise)
run.sh                  run a client in MAME against a live fujinet-pc
emu/                    MAME cart device + apply.sh + SLIP-over-TCP transport;
                        drive.lua/abcheck.lua/soak.lua headless helpers
firmware/               RP2040 firmware; include/fuji_mailbox.h is the spec
testrom/                Z80 clients: fujitest, fujiboot, fujicfg, and the
                        banked self-tests fujibank (APPBANK) and gamebank
                        (256K/512K mapper, FujiNet-free) + HVGLIB.H
tools/                  vendored zmac (PD, see PROVENANCE.md), mkromh, checkrom,
                        mkbanked/mkgame (banked-image packers, self-verifying),
                        soak.sh (stream-and-boot an entire ROM corpus)
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
gcc -Wall -Wextra -Werror -I../include      -o test_bankserve test_bankserve.c ../src/astromap.c && ./test_bankserve
gcc -Wall -Wextra -Werror -I../include      -o test_fujimail test_fujimail.c ../src/fujimail.c ../src/astromap.c && ./test_fujimail

# 2. Clients (fujibank/gamebank also run the packers' self-verification)
./build.sh                      # fujitest fujiboot fujicfg -> build/*.bin
./build.sh fujibank gamebank    # banked self-tests -> fujibank.bin, gamebank{256,512}.bin

# 3. The MAME model (a tree with the astrocde BIOS in roms/). REGENIE=1 is
#    needed only the FIRST time (bus.lua changed); re-run apply.sh after
#    EVERY shared-source edit -- MAME builds the copies, not this tree.
./emu/apply.sh ~/Workspace/mame
make -C ~/Workspace/mame -j$(nproc) NOWERROR=1 REGENIE=1

# 4. Against a live fujinet-pc (its BoIP listener on 127.0.0.1:9995)
./run.sh fujitest               # SSID/version/IP on the console screen
./run.sh fujicfg                # hosts -> browse -> boot over the network

# 5. Banking, headless: the game A/B (our device vs MAME's own mappers --
#    omit -cartslot for the reference run; exact size auto-selects it) and
#    the APPBANK self-test, verdicts printed by emu/abcheck.lua
mame astrocde -cartslot fujinet -cart build/gamebank256.bin -autoboot_script emu/abcheck.lua -video none -sound none
mame astrocde                   -cart build/gamebank256.bin -autoboot_script emu/abcheck.lua -video none -sound none
mame astrocde -cartslot fujinet -cart build/fujibank.bin    -autoboot_script emu/abcheck.lua -video none -sound none

# 6. The soak: DBC-stream and boot EVERY image in a ROM corpus, comparing
#    the served window byte-for-byte against the astromap mapping
tools/soak.sh ~/Workspace/mame/roms/astrocde

# 7. RP2040 firmware
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

Banking (protocol v2), verified the same way (2026-09):

- **Game mapper A/B**: synthetic 256K and 512K images (gamebank walks every
  bank, checks every hotspot read returns its bank number, verifies every
  stamp) PASS identically on this device and on MAME's own
  rom_256k/rom_512k.
- **fujibank end-to-end**: a 32K APPBANK image DBC-streams via fujiboot,
  boots, verifies all 8 pages, and runs a live mailbox transaction WHILE a
  non-zero page is banked in.
- **The soak**: all 133 images in the MAME astrocde ROM corpus (2K-8K,
  mirrored and odd sizes included) stream, boot, and serve a byte-exact
  window (`tools/soak.sh`: 133/133, stream dumps byte-identical).
- The baked-in client is the real fujinet-config CONFIG (5501 bytes), no
  longer the fujicfg testrom stand-in.

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
