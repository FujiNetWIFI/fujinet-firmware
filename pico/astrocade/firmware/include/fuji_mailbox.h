/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Bally Astrocade.
 *
 * Shared by the RP2040 cart firmware, the MAME cart device and (mirrored by
 * hand) the Z80 client in fujilib.inc.
 *
 * The cartridge port carries A0-A12, D0-D7, one pre-decoded Enable (asserted
 * for reads in 0x2000-0x3FFF) and power: no /RD, /WR or /IORQ. So both
 * directions ride the read path:
 *
 *   console -> cart   reads inside a hotspot window; A0-A7 of the address are
 *                     the payload byte (the 512K homebrew mapper and
 *                     AstroBASIC's tape relay use the same idiom).
 *
 *   cart -> console   bytes the cart paints into the ROM window it serves.
 *
 * Stray reads are the hazard. Z80 refresh puts I on A8-A15 and R on A0-A7; if
 * the console's Enable decode does not qualify /RFSH, a program whose I
 * selects a hotspot page sprays phantom reads across offsets 0x00-0x7F of
 * that page. Defenses:
 *   - a REGDATA read with no immediately-preceding REGSEL read is a no-op
 *     (regsel disarms after one use), so an isolated stray mutates nothing;
 *   - a transaction launches only on the SEQ register with a value that is
 *     nonzero and differs from the last acknowledged sequence;
 *   - the ROM-swap trigger lives at offset 0xFE (bit 7 set -- the R register
 *     never reaches it) and fires only after the client armed it;
 *   - our own clients never point I at cart space, so their refresh never
 *     lands there;
 *   - booting an image that does not claim the mailbox clears
 *     fuji_mailbox_active and hotspot decode goes dead for the session.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

#include "fuji_mailbox_proto.h"

/* Cart offsets (0x0000-0x1FFF); the console address is offset + 0x2000.
 * fujilib.inc carries the +0x2000 forms, kept in step by hand. */

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
/* Written last after each slice repaint, with that slice's number. core0
 * repaints while the Z80 runs, so the client polls this for its slice's echo
 * before reading the slice. */
#define FN_R_SLICE_ECHO  0x1C0C

#define FN_R_PAINT_END   0x1CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* 0x1CFC-0x1CFF is never painted. An image carrying "FUJI" here promises
 * 0x1B00-0x1FFF holds no code or data, so the mailbox stays live after it
 * boots; booting any other image disables the mailbox for the session. */
#define FN_R_CLAIM       0x1CFC

/* ---------------- console -> cart: hotspot reads ---------------- */

#define FN_H_REGSEL      0x1D00   /* read +n, n<0x80: arm register n         */
#define FN_H_REGDATA     0x1E00   /* read +n: armed register = n, disarm     */
#define FN_H_DATA        0x1F00   /* read +n: append n to the TX stream      */

#define FN_H_PAGE_MASK   0x1F00   /* offset & this == FN_H_REGSEL: REGSEL page */

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot operations, not registers;
 * refresh strays can't reach them because R bit 7 does not count. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */

/* ---------------- banked images (protocol v2) ----------------
 *
 * GAME (mailbox dead): an unclaimed image of exactly 256K or 512K uses the
 * homebrew mapper, byte-compatible with MAME's rom_256k / rom_512k. Console
 * 0x2000-0x2FFF is fixed to the image's last 4K bank; 0x3000 up to the
 * hotspot base is the switched bank; a read in 0x3FC0-0x3FFF (256K) /
 * 0x3F80-0x3FFF (512K) sets bank = address & mask and returns the new bank
 * number. Those hotspots sit in the FN_H_DATA page, hence mailbox dead.
 * Console RESET does not reset the bank (no reset line on the cart edge); the
 * fixed low half holds the 0x55 sentinel, so restarts still work.
 *
 * APPBANK (mailbox live): a claimed image of 8192 + k*4096 bytes, k >= 1.
 * Image page N = bytes [N*4096, (N+1)*4096). Boot state: page 0 at console
 * 0x2000-0x2FFF (the low half banks), page 1 fixed at 0x3000-0x3FFF and
 * served from the RAM window so mailbox repaints stay visible. Bank select is
 * one read at FN_H_REGSEL + FN_HOT_BANK + page, handled inline like the swap,
 * so the next read sees the new page; the select read's data byte is
 * undefined. Selecting page 1 low is harmless. RESET does not restore page 0,
 * so every selectable page should begin with the 7-byte sentinel header
 * (0x55, DW menu-link, DW name, DW start) whose start vector points at
 * high-half code that re-selects page 0; tools/mkbanked.py stamps this. */
#define FN_HOT_BANK      0x80     /* + page: map image page into the low 4K  */
#define FN_HOT_BANK_LAST 0xEF     /* 0xF0-0xFF stay reserved for special ops */
#define FN_APP_PAGE_SIZE 0x1000
#define FN_APP_MAX_PAGES 112      /* pages 0..111 = ops 0x80..0xEF           */

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
 * The client derives its next sequence number from the cart's FN_R_ACKSEQ + 1
 * (wrapping 255 -> 1; 0 means "never used"), never from a local counter: a
 * console RESET re-zeroes the client's variables but not the cart. */

#include "fuji_mailbox_check.h"

#endif /* FUJI_MAILBOX_H */
