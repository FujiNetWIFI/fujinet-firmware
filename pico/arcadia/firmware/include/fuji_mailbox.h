/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Emerson
 * Arcadia 2001.
 *
 * Single source of truth, shared by the RP2040 cart firmware, the MAME cart
 * device model, and (hand-mirrored) the 2650 console client in fujilib.inc.
 *
 * The Emerson-family cartridge connector carries A0-A13, D0-D7 and power.
 * Nothing else: no write strobe, no clock, no OPREQ, no reset. A12 is the
 * active-low chip select (a real cart ROM's /CE) and A13 picks which 4K
 * block is addressed. So, as on the Astrocade, BOTH directions ride the
 * read path:
 *
 *   console -> cart   reads inside a hotspot window; A0-A7 of the address
 *                     ARE the payload byte.
 *
 *   cart -> console   bytes the cart paints into the ROM window it already
 *                     serves; by the time the console reads them they are
 *                     just ROM.
 *
 * Stray reads are the design hazard. The 2650 has no refresh cycles, but
 * OPREQ is not on the connector, so on real hardware the address bus may
 * carry transients between memory cycles; and a crashed program that
 * wanders into the hotspot pages performs reads by fetching. Defenses, in
 * depth (the Astrocade discipline, unchanged):
 *   - a REGDATA read with no immediately-preceding REGSEL read is a no-op
 *     (regsel disarms after one use), so an isolated stray mutates nothing;
 *   - a transaction launches only on the SEQ register with a value that is
 *     nonzero and differs from the last acknowledged sequence;
 *   - the ROM-swap trigger lives at offset 0xFE and fires only after the
 *     client armed it;
 *   - booting an image that does not claim the mailbox clears
 *     fuji_mailbox_active and hotspot decode goes dead for the session;
 *   - the serving layers act on a hotspot once per stable bus state (the
 *     RP2040 double-samples the address and edge-detects; MAME dispatches
 *     exactly one read per emulated access).
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* Everything below is a CART IMAGE OFFSET (0x0000-0x1FFF). The console
 * address split is NOT linear: image 0x0000-0x0FFF is CPU $0000-$0FFF
 * (block 1, A13=0) and image 0x1000-0x1FFF is CPU $2000-$2FFF (block 2,
 * A13=1) -- console address = offset + 0x1000 for everything in this
 * header. Because A14 does not reach the connector, $4000-$4FFF aliases
 * block 1 and $6000-$6FFF aliases block 2 INCLUDING the hotspots; the cart
 * cannot tell $6DFE from $2DFE, and the MAME device models that on
 * purpose. The 2650 client's equates in fujilib.inc are the console-address
 * forms and must be kept in step by hand.
 *
 * One 2650 wrinkle shapes the client, not this layout: non-branch memory
 * instructions only reach their own 8K page, so page-0 code addresses these
 * pages through 15-bit indirect pointers (LODA,R0 *PTR,Rn) -- one
 * instruction per hotspot read, no code in block 2 required. */

#define FN_WINDOW_SIZE   0x2000   /* the whole cartridge image               */
#define FN_ROM_TOP       0x1B00   /* client code/data must stay below this   */

/* ---------------- cart -> console: repainted ROM ---------------- */

#define FN_R_DATA        0x1B00   /* 256-byte reply slice ($2B00)            */
#define FN_R_SLICE_LEN   0x100
#define FN_R_NSLICES     4        /* x 256 = FUJIMAIL_RX_MAX                 */

#define FN_R_BASE        0x1C00   /* first painted status byte ($2C00)       */
#define FN_R_ACKSEQ      0x1C00   /* echoes SEQ when the reply is ready      */
#define FN_R_STATUS      0x1C01   /* bit0 link up, bit1 busy                 */
#define FN_R_ERR         0x1C02   /* fb_status_t of the last transaction     */
#define FN_R_REPLY_CMD   0x1C03   /* 0x06 ACK / 0x15 NAK                     */
#define FN_R_RXLEN_LO    0x1C04   /* total reply length, LE                  */
#define FN_R_RXLEN_HI    0x1C05
#define FN_R_BOOT_STATE  0x1C06
#define FN_R_BOOT_PCT    0x1C07   /* 0-100, for a MOUNT_IMAGE progress bar   */
#define FN_R_BOOT_ERR    0x1C08
#define FN_R_MAGIC0      0x1C09   /* 'F' -- cart presence check              */
#define FN_R_MAGIC1      0x1C0A   /* 'N'                                     */
#define FN_R_PROTO_VER   0x1C0B
#define FN_PROTO_VER     1        /* flat images only; nothing >8K exists on
                                   * this platform (A14 is not on the pins)  */
/* Published LAST after every slice repaint, with the slice number just
 * painted. The repaint is asynchronous on real hardware (core0 does it while
 * the 2650 keeps running), so the client selects a slice and polls this
 * until it echoes; reading the slice before that races the repaint. */
#define FN_R_SLICE_ECHO  0x1C0C

#define FN_R_PAINT_END   0x1CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* 0x1CFC-0x1CFF is never painted: it is where a cartridge image declares
 * itself. An image carrying "FUJI" here promises that 0x1B00-0x1FFF holds no
 * code or data, so the mailbox may keep painting and decoding after it
 * boots. An ordinary game uses that space for its own bytes, which is why
 * booting one disables the mailbox for the session. */
#define FN_R_CLAIM       0x1CFC
#define FN_R_CLAIM_LEN   4
#define FN_R_CLAIM_SIG   "FUJI"

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* Reply-window stability invariant: the reply slice and RXLEN are repainted
 * ONLY by a SEQ commit or an RXSLICE select. Between those, a client may
 * stream bytes straight out of the reply window -- e.g. copy a directory
 * entry from $2B00+i into the TX page while building the next transaction
 * -- without a RAM bounce. The 288 bytes of console RAM make this
 * zero-copy idiom the difference between fitting and not. */

/* ---------------- console -> cart: hotspot reads ---------------- */

#define FN_H_REGSEL      0x1D00   /* read +n, n<0x80: arm register n ($2D00) */
#define FN_H_REGDATA     0x1E00   /* read +n: armed register = n, disarm     */
#define FN_H_DATA        0x1F00   /* read +n: append n to the TX stream      */

#define FN_H_PAGE_MASK   0x1F00   /* offset & this == FN_H_REGSEL: REGSEL page */

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot special operations, not
 * register numbers, kept in the bit7-set half of the page as on the
 * Astrocade. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */

/* Registers reached through an FN_H_REGSEL / FN_H_REGDATA read pair.
 * 0x00-0x0F mirrors the O2/Astrocade numbering where the meaning matches. */
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
 * (Identical to the O2/Astrocade stream; the payload length is whatever
 * remains.) */
#define FN_TX_MAX        320

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2        /* image staged; arm BOOTLOCK, then swap   */
#define FN_BOOT_FAILED   0x80

/* FN_R_BOOT_ERR values. */
#define FN_BOOT_ERR_TOOBIG    1   /* image over 8K: not mappable here        */
#define FN_BOOT_ERR_TRUNCATED 2

/* FN_R_ERR values; mirrors fb_status_t in fujibus.h. */
#define FN_ERR_OK        0
#define FN_ERR_NOLINK    1
#define FN_ERR_TIMEOUT   2
#define FN_ERR_BADFRAME  3
#define FN_ERR_TOOBIG    4

/* Booting a staged image, from the client's side:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC (a REGSEL/REGDATA pair);
 *   3. copy the 11-byte swap stub into free RAM at $18E0 and BCTA to it;
 *   4. the stub clears the PSW (power-on-likeness for the next image), then
 *      reads console $2DFE (FN_H_REGSEL + FN_HOT_SWAP) through a 15-bit
 *      indirect pointer: the cart flips to the staged image between this
 *      read and the next;
 *   5. the stub does BCTA,UN $0000 -- the new image's own header runs.
 * The stub must run from RAM: the swap replaces every byte of the cart
 * window, including whatever code triggered it. Canonical bytes (see
 * fujilib.inc): 20 92 93 0C 98 E9 1F 00 00 2D FE at $18E0-$18EA.
 *
 * After the swap: a claiming image keeps the mailbox live; a game image
 * (anything without the claim) kills it for the session. Games of 4K or
 * less never even occupy block 2, but they do not claim, so the rule is the
 * same for all of them.
 *
 * CRITICAL, inherited from the Intellivision/O2/Astrocade bring-ups: the
 * client derives its next sequence number from the cart's own persisted
 * FN_R_ACKSEQ + 1 (wrapping 255 -> 1; 0 is reserved as "never used"), never
 * from a program-local counter. A console RESET restarts the client and
 * re-zeroes its variables but does NOT reset the cart. */

#endif /* FUJI_MAILBOX_H */
