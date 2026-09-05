/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Bally Astrocade.
 *
 * Single source of truth, shared by the RP2040 cart firmware, the MAME cart
 * device model, and (hand-mirrored) the Z80 console client in fujilib.inc.
 *
 * The Astrocade cartridge port is the most asymmetric in the family: it
 * carries A0-A12, D0-D7, one pre-decoded Enable (asserted for reads in
 * 0x2000-0x3FFF) and power. No /RD, no /WR, no /IORQ. So BOTH directions ride
 * the read path:
 *
 *   console -> cart   reads inside a hotspot window; A0-A7 of the address ARE
 *                     the payload byte. Established Astrocade idiom: the 512K
 *                     homebrew mapper selects banks with reads at 0x3F80-3FFF
 *                     and AstroBASIC toggles its tape relay the same way.
 *
 *   cart -> console   bytes the cart paints into the ROM window it already
 *                     serves; by the time the console reads them they are just
 *                     ROM (the O2 "reads are free" rule, now used both ways).
 *
 * Stray reads are the design hazard here, not stray writes. Z80 refresh
 * cycles put I on A8-A15 and R on A0-A7; if the console's Enable decode does
 * not qualify /RFSH, a program whose I register selects one of the hotspot
 * pages sprays phantom reads across offsets 0x00-0x7F of that page. Defenses,
 * in depth:
 *   - a REGDATA read with no immediately-preceding REGSEL read is a no-op
 *     (regsel disarms after one use), so an isolated stray mutates nothing;
 *   - a transaction launches only on the SEQ register with a value that is
 *     nonzero and differs from the last acknowledged sequence;
 *   - the ROM-swap trigger lives at offset 0xFE (bit 7 set -- the R register
 *     never reaches it) and fires only after the client armed it;
 *   - our own clients keep I pointed at screen RAM (0x4F), so refresh never
 *     lands in cart space at all;
 *   - booting an image that does not claim the mailbox clears
 *     fuji_mailbox_active and hotspot decode goes dead for the session.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* Everything below is a CART OFFSET (0x0000-0x1FFF); the console address is
 * offset + 0x2000. The Z80 client's equates in fujilib.inc are the +0x2000
 * forms and must be kept in step by hand. */

#define FN_WINDOW_SIZE   0x2000   /* the whole cartridge window              */
#define FN_ROM_TOP       0x1B00   /* client code/data must stay below this   */

/* ---------------- cart -> console: repainted ROM ---------------- */

#define FN_R_DATA        0x1B00   /* 256-byte reply slice                    */
#define FN_R_SLICE_LEN   0x100
#define FN_R_NSLICES     4        /* x 256 = FUJIMAIL_RX_MAX                 */

#define FN_R_BASE        0x1C00   /* first painted status byte              */
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
#define FN_PROTO_VER     2        /* the value painted there; 2 = banking    */
/* Published LAST after every slice repaint, with the slice number just
 * painted. The repaint is asynchronous on real hardware (core0 does it while
 * the Z80 keeps running), so the client selects a slice and polls this until
 * it echoes; reading the slice before that races the repaint. The O2 protocol
 * lacks this byte and its emulator model admits it cannot catch a client
 * that forgets to poll -- here the byte makes polling possible at all. */
#define FN_R_SLICE_ECHO  0x1C0C

#define FN_R_PAINT_END   0x1CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* 0x1CFC-0x1CFF is never painted: it is where a cartridge image declares
 * itself. An image carrying "FUJI" here promises that 0x1B00-0x1FFF holds no
 * code or data, so the mailbox may keep painting and decoding after it boots.
 * An ordinary game uses that space for its own bytes, which is why booting
 * one disables the mailbox for the session. */
#define FN_R_CLAIM       0x1CFC
#define FN_R_CLAIM_LEN   4
#define FN_R_CLAIM_SIG   "FUJI"

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* ---------------- console -> cart: hotspot reads ---------------- */

#define FN_H_REGSEL      0x1D00   /* read +n, n<0x80: arm register n         */
#define FN_H_REGDATA     0x1E00   /* read +n: armed register = n, disarm     */
#define FN_H_DATA        0x1F00   /* read +n: append n to the TX stream      */

#define FN_H_PAGE_MASK   0x1F00   /* offset & this == FN_H_REGSEL: REGSEL page */

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot special operations, not
 * register numbers. Placed in the bit7-set half so default-R-register
 * refresh strays (R bit 7 does not count) can never reach them. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */

/* ---------------- banked images (protocol v2) ----------------
 *
 * Two schemes, mutually exclusive by construction.
 *
 * GAME (mailbox dead): an image of exactly 256K or 512K with no claim uses
 * the established homebrew mapper, byte-compatible with MAME's rom_256k /
 * rom_512k. Console 0x2000-0x2FFF is fixed to the LAST 4K bank of the image;
 * 0x3000 up to the hotspot base is the switched 4K bank; a read anywhere in
 * 0x3FC0-0x3FFF (256K) / 0x3F80-0x3FFF (512K) sets bank = address & mask AND
 * returns the new bank number as the data byte. Those hotspots sit inside
 * the FN_H_DATA page, which is exactly why game banking exists only with the
 * mailbox dead -- a claim-less boot already guarantees that. Bank state is
 * NOT reset by console RESET (the cart edge carries no reset line); the
 * fixed low half holds the 0x55 sentinel, so restarts still work.
 *
 * APPBANK (mailbox fully live): a claimed image of 8192 + k*4096 bytes,
 * k >= 1. Image page N = bytes [N*4096, (N+1)*4096). Boot state: page 0 at
 * console 0x2000-0x2FFF (the LOW half is what banks), page 1 fixed at
 * 0x3000-0x3FFF and always served from the RAM window so mailbox repaints
 * stay visible. Bank select is ONE read at FN_H_REGSEL + FN_HOT_BANK + page,
 * handled inline by the bus-serving layer like the swap, so the very next
 * read sees the new page; the data byte the select read returns is
 * undefined. Selecting page 1 low is legal and harmless. A console RESET
 * does not restore page 0, so every selectable page should begin with the
 * 7-byte sentinel header (0x55, DW menu-link, DW name, DW start) whose start
 * vector points at high-half code that re-selects page 0 --
 * tools/mkbanked.py stamps this. */
#define FN_HOT_BANK      0x80     /* + page: map image page into the low 4K  */
#define FN_HOT_BANK_LAST 0xEF     /* 0xF0-0xFF stay reserved for special ops */
#define FN_APP_PAGE_SIZE 0x1000
#define FN_APP_MAX_PAGES 112      /* pages 0..111 = ops 0x80..0xEF           */

/* Registers reached through an FN_H_REGSEL / FN_H_REGDATA read pair.
 * 0x00-0x0F mirrors the O2 numbering where the meaning matches. */
#define FN_REG_DEVICE    0x00     /* FujiBus device id, e.g. 0x70            */
#define FN_REG_CMD       0x01     /* FujiBus command id                      */
#define FN_REG_NPARAM    0x02     /* number of parameters in the TX stream   */
#define FN_REG_DATA_RST  0x05     /* any value: rewind the TX write pointer  */
#define FN_REG_RXSLICE   0x06     /* which reply slice FN_R_DATA shows       */
/* 0x10 up is Astrocade-specific. */
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
 * (Identical to the O2 stream; the payload length is whatever remains.) */
#define FN_TX_MAX        320

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
 *   3. copy the ~10-byte swap stub into top of screen RAM and jump to it;
 *   4. the stub reads console 0x3DFE (FN_H_REGSEL + FN_HOT_SWAP): the cart
 *      flips to the staged image between this read and the next one;
 *   5. the stub does JP 0 -- the OS cold-starts, walks the new image's 0x55
 *      sentinel menu, and the game is one keypress away.
 * The stub must run from RAM: the swap replaces every byte of the cart
 * window, including whatever code triggered it.
 *
 * After the swap: an APPBANK image keeps the mailbox live with page 0 in the
 * low half; a GAME image kills the mailbox and its own hotspots take over.
 *
 * CRITICAL, inherited from the Intellivision/O2 bring-ups: the client derives
 * its next sequence number from the cart's own persisted FN_R_ACKSEQ + 1
 * (wrapping 255 -> 1; 0 is reserved as "never used"), never from a
 * program-local counter. A console RESET restarts the client and re-zeroes
 * its variables but does NOT reset the cart. */

#endif /* FUJI_MAILBOX_H */
