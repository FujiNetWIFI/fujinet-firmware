/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the ColecoVision.
 *
 * Single source of truth, shared by the RP2040 cart firmware, the MAME cart
 * device model, and (hand-mirrored) the Z80 console client in testrom/fujilib.h.
 *
 * The ColecoVision cartridge port carries A0-A14, D0-D7, four pre-decoded
 * active-low chip selects (/8000 /A000 /C000 /E000, one per 8K block), GND and
 * +5V. Nothing else: no /RD, no /WR, no /MREQ, no /IORQ, no clock, no reset.
 * Two facts fall out of the console's decoder (74LS138 U5: A13/A14/A15 on the
 * select inputs, E1 = /MREQ, E2 = /RFSH, E3 = AUX_DECODE_1) and they shape
 * everything here:
 *
 *   - the selects are asserted for reads AND writes and the cart cannot tell
 *     them apart, which is why every real ColecoVision mapper -- MegaCart,
 *     Activision, X-in-1 -- derives its state from the ADDRESS alone;
 *   - the selects are inhibited during Z80 refresh, so the R register's
 *     traffic on A0-A7 can never reach us. The Astrocade had to defend
 *     against exactly that; we get it for free.
 *
 * So, as on the Astrocade and the Arcadia, BOTH directions ride the read path:
 *
 *   console -> cart   reads inside a hotspot window; A0-A7 of the address ARE
 *                     the payload byte.
 *
 *   cart -> console   bytes the cart paints into the ROM window it already
 *                     serves; by the time the console reads them they are just
 *                     ROM (the O2 "reads are free" rule, used both ways).
 *
 * Stray reads remain the design hazard: a crashed program that wanders into
 * the hotspot pages performs reads by fetching, and the ColecoVision's vblank
 * interrupt is wired to the Z80's /NMI, so it cannot be held off with DI and
 * WILL land in the middle of a transaction. Defenses, in depth:
 *   - a REGDATA read with no immediately-preceding REGSEL read is a no-op
 *     (regsel disarms after one use), so an isolated stray mutates nothing;
 *   - an NMI that touches only the BIOS, the VDP ports and RAM cannot disturb
 *     an armed REGSEL or an append-only TX stream, which is why the whole
 *     client-side rule is "the NMI handler must never read FN_ROM_TOP and up"
 *     -- tools/checkrom.py enforces the image half of that;
 *   - a transaction launches only on the SEQ register with a value that is
 *     nonzero and differs from the last acknowledged sequence;
 *   - the ROM-swap trigger lives at offset 0xFE and fires only after the
 *     client armed it;
 *   - booting an image that does not claim the mailbox clears
 *     fuji_mailbox_active and hotspot decode goes dead for the session;
 *   - the serving layers act on a hotspot once per chip-select assertion (the
 *     RP2040 spins until the select deasserts; MAME dispatches exactly one
 *     read per emulated access).
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* Everything below is a CART IMAGE OFFSET (0x0000-0x7FFF); the console address
 * is offset + 0x8000, linearly -- unlike the Arcadia there is no split, since
 * A0-A14 all reach the connector. The Z80 client's equates in testrom/fujilib.h
 * are the +0x8000 forms and must be kept in step by hand. */

#define FN_WINDOW_SIZE   0x8000   /* the whole cartridge window              */
#define FN_ROM_TOP       0x7800   /* client code/data must stay below this   */

/* ---------------- cart -> console: repainted ROM ---------------- */

/* The whole reply, in one piece, at $F800-$FBFF. Every other port in this
 * family pages a 1K reply through a 256-byte slice because its window was too
 * small to do otherwise; here the window is 32K and the CONSOLE is what is
 * scarce. The ColecoVision has 1K of RAM, of which the BIOS keeps $73B9-$73FF
 * and a z88dk client's BSS and stack take most of the rest -- call it 700
 * usable bytes. Handing the client the entire reply as directly-addressable
 * ROM removes the RAM bounce buffer AND the slice-echo poll from every
 * transaction, which is the difference between a directory browser fitting and
 * not. FN_REG_RXSLICE and FN_R_SLICE_ECHO stay in the register file so
 * fujimail.c compiles verbatim; with one slice they are simply always 0. */
#define FN_R_DATA        0x7800   /* the whole reply ($F800)                 */
#define FN_R_SLICE_LEN   0x400
#define FN_R_NSLICES     1        /* x 1024 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x7C00   /* first painted status byte ($FC00)       */
#define FN_R_ACKSEQ      0x7C00   /* echoes SEQ when the reply is ready      */
#define FN_R_STATUS      0x7C01   /* bit0 link up, bit1 busy                 */
#define FN_R_ERR         0x7C02   /* fb_status_t of the last transaction     */
#define FN_R_REPLY_CMD   0x7C03   /* 0x06 ACK / 0x15 NAK                     */
#define FN_R_RXLEN_LO    0x7C04   /* total reply length, LE                  */
#define FN_R_RXLEN_HI    0x7C05
#define FN_R_BOOT_STATE  0x7C06
#define FN_R_BOOT_PCT    0x7C07   /* 0-100, for a MOUNT_IMAGE progress bar   */
#define FN_R_BOOT_ERR    0x7C08
#define FN_R_MAGIC0      0x7C09   /* 'F' -- cart presence check              */
#define FN_R_MAGIC1      0x7C0A   /* 'N'                                     */
#define FN_R_PROTO_VER   0x7C0B
#define FN_PROTO_VER     1        /* flat client images; game mappers below  */
/* Published LAST after every slice repaint, with the slice number just
 * painted. The repaint is asynchronous on real hardware (core0 does it while
 * the Z80 keeps running), so the client selects a slice and polls this until
 * it echoes; reading the slice before that races the repaint. */
#define FN_R_SLICE_ECHO  0x7C0C

#define FN_R_PAINT_END   0x7CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* 0x7CFC-0x7CFF is never painted: it is where a cartridge image declares
 * itself. An image carrying "FUJI" here promises that 0x7800-0x7FFF holds no
 * code or data, so the mailbox may keep painting and decoding after it boots.
 * An ordinary game uses that space for its own bytes -- or, far more often,
 * is simply smaller than 32K and does not reach it at all -- which is why
 * booting one disables the mailbox for the session. */
#define FN_R_CLAIM       0x7CFC
#define FN_R_CLAIM_LEN   4
#define FN_R_CLAIM_SIG   "FUJI"

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* Reply-window stability invariant: the reply and RXLEN are repainted ONLY by
 * a SEQ commit or an RXSLICE select. Between those, a client may stream bytes
 * straight out of the reply window -- copying a directory entry from $F800+i
 * into the TX page while building the next transaction, say -- without a RAM
 * bounce buffer. Reads of the TX page do not disturb it. With ~700 usable
 * bytes of console RAM this is not an optimisation, it is what makes a
 * directory browser possible at all. */

/* ---------------- console -> cart: hotspot reads ---------------- */

#define FN_H_REGSEL      0x7D00   /* read +n, n<0x80: arm register n ($FD00) */
#define FN_H_REGDATA     0x7E00   /* read +n: armed register = n, disarm     */
#define FN_H_DATA        0x7F00   /* read +n: append n to the TX stream      */

#define FN_H_PAGE_MASK   0x7F00   /* offset & this == FN_H_REGSEL: REGSEL page */

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot special operations, not register
 * numbers, kept in the bit7-set half as on the Astrocade and the Arcadia. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */

/* ---------------- game mappers ----------------
 *
 * A booted image that does not claim the mailbox gets served byte-exactly,
 * with whichever ColecoVision mapper its size (or its .cfg sibling) selects:
 * flat, MegaCart, X-in-1, Activision or Opcode SGC. Every one of those puts
 * its bank hotspots inside $FF00 -- the same page as FN_H_DATA. That is not a
 * collision but the same mutual exclusion the Astrocade relies on: a game
 * image carries no claim, so the mailbox is already dead before any mapper
 * decode goes live, and a claimed image is flat by construction. The mapper
 * rules themselves live in colmap.h, next to the code that implements them.
 *
 * Client images are therefore always flat and always exactly FN_WINDOW_SIZE
 * bytes: the claim has to land at a fixed offset that is inside the image, and
 * the reserved pages have to exist so the cart can paint them. */

/* Registers reached through an FN_H_REGSEL / FN_H_REGDATA read pair.
 * 0x00-0x0F mirrors the O2/Astrocade/Arcadia numbering. */
#define FN_REG_DEVICE    0x00     /* FujiBus device id, e.g. 0x70            */
#define FN_REG_CMD       0x01     /* FujiBus command id                      */
#define FN_REG_NPARAM    0x02     /* number of parameters in the TX stream   */
#define FN_REG_DATA_RST  0x05     /* any value: rewind the TX write pointer  */
#define FN_REG_RXSLICE   0x06     /* which reply slice FN_R_DATA shows       */
/* 0x10 up matches the Astrocade numbering. */
#define FN_REG_SEQ       0x10     /* nonzero, != ACKSEQ: launch transaction  */
#define FN_REG_BOOTLOCK  0x11     /* FN_BOOTLOCK_MAGIC: arm the ROM swap     */
#define FN_REG_BOOTSEL_1 0x12     /* FN_BOOTSEL_MAGIC1, then...              */
#define FN_REG_BOOTSEL_2 0x13     /* ...FN_BOOTSEL_MAGIC2: reboot to BOOTSEL */

#define FN_BOOTLOCK_MAGIC 0xB5
/* Two consecutive register writes, each with its own REGSEL pair, are needed
 * to reboot the cart into UF2 mode; one stray read can never do it. */
#define FN_BOOTSEL_MAGIC1 0xB5
#define FN_BOOTSEL_MAGIC2 0x4A

/* The FN_H_DATA stream is, in order:
 *     NPARAM x { size byte (1|2|4), then that many value bytes, little-endian }
 *     then the raw payload
 * (Identical to the O2/Astrocade/Arcadia stream; the payload length is
 * whatever remains.) */
#define FN_TX_MAX        320

/* DBC push stream ids, matching lib/media/rs232/diskTypeROM.cpp. Stream 0 is
 * the cartridge image; stream 1 is its optional .cfg sibling, which every
 * other cartridge in this family discards and this one actually reads -- see
 * colmap_parse_cfg. */
#define FN_STREAM_ROM    0
#define FN_STREAM_CFG    1

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2        /* image staged; arm BOOTLOCK, then swap   */
#define FN_BOOT_FAILED   0x80

/* FN_R_BOOT_ERR values. */
#define FN_BOOT_ERR_TOOBIG    1
#define FN_BOOT_ERR_TRUNCATED 2
#define FN_BOOT_ERR_NOMAP     3
#define FN_BOOT_ERR_STOREBUSY 4   /* only fitting store is being served from */

/* FN_R_ERR values; mirrors fb_status_t in fujibus.h. */
#define FN_ERR_OK        0
#define FN_ERR_NOLINK    1
#define FN_ERR_TIMEOUT   2
#define FN_ERR_BADFRAME  3
#define FN_ERR_TOOBIG    4

/* Booting a staged image, from the client's side:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC (a REGSEL/REGDATA pair);
 *   3. blank the screen and CLEAR VDP register 1 bit 5, so the vblank NMI
 *      cannot fire through $8021 while the image underneath it is changing;
 *   4. copy the swap stub into console RAM at $6000 (the 1K at $6000-$63FF is
 *      mirrored to $7FFF and the BIOS stack lives at its top, $73B9 == $63B9,
 *      so the bottom is free) and JP to it;
 *   5. the stub reads console $FDFE (FN_H_REGSEL + FN_HOT_SWAP): the cart
 *      flips to the staged image between this read and the next one;
 *   6. the stub does JP $0000 -- the BIOS cold-starts, re-reads the new
 *      image's own $8000 header, and $55AA / $AA55 behave exactly as they
 *      would on a real cartridge.
 * The stub must run from RAM: the swap replaces every byte of the cart
 * window, including whatever code triggered it.
 *
 * CRITICAL, inherited from the Intellivision/O2/Astrocade/Arcadia bring-ups:
 * the client derives its next sequence number from the cart's own persisted
 * FN_R_ACKSEQ + 1 (wrapping 255 -> 1; 0 is reserved as "never used"), never
 * from a program-local counter. A console RESET restarts the client and
 * re-zeroes its variables but does NOT reset the cart. */

#endif /* FUJI_MAILBOX_H */
