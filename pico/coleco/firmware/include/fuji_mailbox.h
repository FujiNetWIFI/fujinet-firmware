/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the ColecoVision, shared by the
 * cart firmware and the MAME device, and hand-mirrored in testrom/fujilib.h.
 *
 * The connector carries A0-A14, D0-D7, four chip selects and power: no /RD, /WR or clock.
 * The selects come from a '138 gated by /MREQ and /RFSH, so they assert on writes too and
 * never during refresh. Both directions ride reads: console -> cart, A0-A7 of a hotspot
 * read are the payload byte; cart -> console, the cart paints bytes into the ROM window
 * it serves.
 *
 * Stray reads are the hazard: the vblank NMI cannot be masked and will land mid-
 * transaction, and a crashed program can fetch from the hotspot pages.
 *   - a REGDATA read without an immediately preceding REGSEL is a no-op;
 *   - the NMI handler must never read FN_ROM_TOP and up (tools/checkrom.py checks images);
 *   - a transaction launches only on SEQ with a nonzero value unlike the last ACKSEQ;
 *   - the swap at offset 0xFE fires only after the client armed it;
 *   - booting an image without the claim kills hotspot decode for the session;
 *   - a hotspot acts once per chip-select assertion.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

#include "fuji_mailbox_proto.h"

/* Cart image offsets; the console address is offset + 0x8000. */

#define FN_WINDOW_SIZE   0x8000   /* the whole cartridge window              */
#define FN_ROM_TOP       0x7800   /* client code/data must stay below this   */

/* ---------------- cart -> console: repainted ROM ---------------- */

/* The whole reply in one 1K slice: the console has ~700 usable bytes of RAM, so the
 * client reads replies in place instead of through a bounce buffer. With one slice
 * FN_REG_RXSLICE and FN_R_SLICE_ECHO are always 0. */
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
#define FN_PROTO_VER     1        /* flat client images                      */
#define FN_R_SLICE_ECHO  0x7C0C

#define FN_R_PAINT_END   0x7CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* Never painted: an image carrying "FUJI" here promises 0x7800-0x7FFF holds none of its
 * code or data, so the mailbox stays live after it boots. Any other image ends it. */
#define FN_R_CLAIM       0x7CFC

/* The reply and RXLEN change only on a SEQ commit or RXSLICE select, so a client may copy
 * straight from $F800+i into the TX page; with ~700 bytes of console RAM it must. */

/* ---------------- console -> cart: hotspot reads ---------------- */

#define FN_H_REGSEL      0x7D00   /* read +n, n<0x80: arm register n ($FD00) */
#define FN_H_REGDATA     0x7E00   /* read +n: armed register = n, disarm     */
#define FN_H_DATA        0x7F00   /* read +n: append n to the TX stream      */

#define FN_H_PAGE_MASK   0x7F00   /* offset & this == FN_H_REGSEL: REGSEL page */

/* REGSEL offsets 0x80-0xFF are one-shot operations, not registers. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed only      */

/* Every game mapper's bank hotspots sit in $FF00-$FFFF too, the FN_H_DATA page. They never
 * meet: a game carries no claim, so the mailbox is dead before its mapper goes live, and
 * a claiming image is flat. So a client image is always exactly FN_WINDOW_SIZE bytes. */

/* Booting a staged image:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC;
 *   3. blank the screen and clear VDP register 1 bit 5, so the vblank NMI cannot fire
 *      through $8021 while the image changes;
 *   4. copy the swap stub to console RAM at $6000 and JP to it;
 *   5. the stub reads $FDFE and the cart flips to the staged image before the next read;
 *   6. the stub does JP $0000, and the BIOS cold-starts the new image from its header.
 * The client numbers its transactions from the cart's FN_R_ACKSEQ + 1, never a counter of
 * its own, since a console reset doesn't reset the cart. */

#include "fuji_mailbox_check.h"

#endif /* FUJI_MAILBOX_H */
