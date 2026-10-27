/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Arcadia 2001, shared by the
 * cart firmware and the MAME device, and hand-mirrored in testrom/fujilib.inc.
 *
 * The connector carries only A0-A13, D0-D7 and power; A12 low is the chip select. Both
 * directions ride reads: console -> cart, A0-A7 of a hotspot read are the payload byte;
 * cart -> console, the cart paints bytes into the ROM window it serves.
 *
 * Stray reads are the hazard: OPREQ is not on the connector, so the bus may carry
 * transients between cycles, and a crashed program can fetch from the hotspot pages.
 *   - a REGDATA read without an immediately preceding REGSEL is a no-op, except that a
 *     single read at $2D80+n selects page n of an APPBANK image;
 *   - a transaction launches only on SEQ with a nonzero value unlike the last ACKSEQ;
 *   - the swap at offset 0xFE fires only after the client armed it;
 *   - booting an image without the claim kills hotspot decode for the session;
 *   - a hotspot acts once per stable bus state.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

#include "fuji_mailbox_proto.h"

/* Cart image offsets. Image 0x1000-0x1FFF is console $2000-$2FFF, so the console address
 * of everything at 0x1000 and up here is offset + 0x1000. $4000/$6000 alias the two
 * blocks, hotspots included. The 2650 reaches these pages from page-0 code through
 * 15-bit indirect pointers (LODA,R0 *PTR,Rn). */

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
#define FN_PROTO_VER     2        /* 2: APPBANK pages, cart RAM, the tick   */
/* Painted last, with the slice number, after each slice repaint; the repaint is
 * asynchronous, so the client polls this until it echoes before reading the slice. */
#define FN_R_SLICE_ECHO  0x1C0C
                                  /* 0x1C0D reserved, reads 0                */
#define FN_R_TICK_LO     0x1C0E   /* free-running 60 Hz count, LE ($2C0E)    */
#define FN_R_TICK_HI     0x1C0F

#define FN_R_PAINT_END   0x1CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* The tick: the 2650 has no interrupt the cart could drive and SENSE only says "in
 * vblank", so the cart counts 60 Hz frames from power-up, through transactions. Read HI,
 * LO, HI and retry if the HIs differ. Not painted while the mailbox is dead. Under MAME it
 * stands still across a transaction, which runs inside one read. */

/* Never painted. "FUJI" here promises 0x1B00-0x1FFF holds no code or data, so the mailbox
 * stays live after the image boots; an image without it kills the mailbox. */
#define FN_R_CLAIM       0x1CFC

/* The reply slice and RXLEN change only on a SEQ commit or RXSLICE select, so a client may
 * copy straight from $2B00+i into the TX page; with 288 bytes of console RAM it must. */

/* ---------------- console -> cart: hotspot reads ---------------- */

#define FN_H_REGSEL      0x1D00   /* read +n, n<0x80: arm register n ($2D00) */
#define FN_H_REGDATA     0x1E00   /* read +n: armed register = n, disarm     */
#define FN_H_DATA        0x1F00   /* read +n: append n to the TX stream      */

#define FN_H_PAGE_MASK   0x1F00   /* offset & this == FN_H_REGSEL: REGSEL page */

/* REGSEL offsets 0x80-0xFF are one-shot operations, not registers. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */
#define FN_HOT_BANK      0x80     /* read +n, n < 32: window shows page n    */
#define FN_HOT_BANK_LAST 0x9F

/* ---------------- protocol 2: APPBANK images ----------------
 *
 * A client bigger than 6,912 bytes ships as 8192 + k * 2048 bytes (1 <= k <= 31) with the
 * claim; console $0800-$0FFF shows one 2K page at a time:
 *
 *   image 0x0000-0x05FF   $0000-$05FF   fixed
 *   image 0x0600-0x07FF   $0600-$07FF   cart RAM, low half (RAM-L)
 *   image 0x0800-0x0FFF   $0800-$0FFF   the window; page 0 lives here
 *   image 0x1000-0x18FF   $2000-$28FF   fixed
 *   image 0x1900-0x1AFF   $2900-$2AFF   cart RAM, high half (RAM-H)
 *   image 0x1B00-0x1FFF   $2B00-$2FFF   the mailbox, as on a flat image
 *   image 0x2000 + (n-1) * 0x800        page n
 *
 * A read of $2D80+n selects page n for the very next fetch, so make it from fixed code. A
 * missing page reads 0xFF. The swap maps page 0; a console RESET changes neither the page
 * nor cart RAM, so start code selects page 0 itself.
 *
 * Cart RAM is the image's own bytes in those ranges, made writable; its 10-bit space is
 * 0x000-0x1FF = RAM-L, 0x200-0x3FF = RAM-H. A write is still a read:
 *
 *   REGSEL FN_REG_RAMPTR0..3, REGDATA v   pointer = 0x000/0x100/0x200/0x300 + v
 *   REGSEL FN_REG_RAMPOKE, then REGDATA v, REGDATA w, ...
 *                                         stores v, w, ... from the pointer (wrapping
 *                                         at 0x400) until any REGSEL below 0x80
 *
 * so n bytes cost n + 4 reads. The bus layer takes these, so they never reach fujimail.c,
 * whose pairing disarms after one REGDATA. A client ends each burst with a bare REGSEL
 * FN_REG_RAMPTR0, so a stray REGDATA can move the pointer but never store. Bank selects do
 * not end a burst. On a flat image all of these are forwarded as in protocol 1. */
#define FN_APP_WIN       0x0800   /* image offset (= console address) of the window */
#define FN_APP_PAGE      0x0800   /* 2K pages                                */
#define FN_APP_NPAGES    32       /* page 0 + up to 31 more                  */
#define FN_APP_MAX       (0x2000 + (FN_APP_NPAGES - 1) * FN_APP_PAGE)

#define FN_RAM_L         0x0600   /* image offsets; console $0600, $2900     */
#define FN_RAM_H         0x1900
#define FN_RAM_HALF      0x0200
#define FN_RAM_SIZE      0x0400

#define FN_REG_RAMPTR0   0x14     /* pointer = 0x000 + value                 */
#define FN_REG_RAMPTR3   0x17     /* pointer = 0x300 + value                 */
#define FN_REG_RAMPOKE   0x18     /* each REGDATA stores and advances        */

/* Booting a staged image:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC;
 *   3. copy the 11-byte swap stub to RAM at $18E0 and BCTA to it (the swap replaces the
 *      whole window); its bytes are 20 92 93 0C 98 E9 1F 00 00 2D FE;
 *   4. the stub clears the PSW, reads $2DFE through a 15-bit indirect pointer, and the
 *      cart flips to the staged image before the next read;
 *   5. the stub does BCTA,UN $0000.
 * A claiming image finds the status page as the previous client left it; any other image
 * kills the mailbox.
 *
 * The next SEQ is the cart's FN_R_ACKSEQ + 1 (255 wraps to 1; 0 is never used), never a
 * local counter: a console RESET does not reset the cart. */

#include "fuji_mailbox_check.h"

#endif /* FUJI_MAILBOX_H */
