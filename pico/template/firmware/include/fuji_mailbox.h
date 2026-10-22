/* fuji_mailbox.h -- the template cartridge's mailbox: where in its 8K window the status
 * fields, reply slices and hotspot pages sit. The firmware, the MAME device and the
 * console client (by hand) all use this one map.
 *
 * A ROM window carries no write strobe, so both directions are reads:
 *   console -> cart   a read inside a hotspot page; A0-A7 of the address are the byte.
 *   cart -> console   bytes the cart paints into the window it already serves.
 *
 * Everything below is an offset into the window. Keep the client's equates (console
 * address = window base + offset) in step by hand.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

#include "fuji_mailbox_proto.h"

#define FN_WINDOW_SIZE   0x2000   /* the whole cartridge image               */
#define FN_ROM_TOP       0x1B00   /* client code and data stay below this    */

/* ---------------- cart -> console: repainted ROM ---------------- */

#define FN_R_DATA        0x1B00   /* 256-byte reply slice                    */
#define FN_R_SLICE_LEN   0x100
#define FN_R_NSLICES     4        /* x 256 = FUJIMAIL_RX_MAX                 */

#define FN_R_BASE        0x1C00   /* first painted status byte               */
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
#define FN_PROTO_VER     1
/* Published last after each slice repaint: the client selects a slice and polls this
 * until it echoes, since core0 repaints while the console runs. */
#define FN_R_SLICE_ECHO  0x1C0C

#define FN_R_PAINT_END   0x1CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* Never painted: an image carrying "FUJI" here promises 0x1B00-0x1FFF holds none of its
 * code or data, so the mailbox stays live after it boots. Any other image ends it. */
#define FN_R_CLAIM       0x1CFC

/* ---------------- console -> cart: hotspot reads ---------------- */

#define FN_H_REGSEL      0x1D00   /* read +n, n<0x80: arm register n         */
#define FN_H_REGDATA     0x1E00   /* read +n: armed register = n, disarm     */
#define FN_H_DATA        0x1F00   /* read +n: append n to the TX stream      */

#define FN_H_PAGE_MASK   0x1F00   /* offset & this == FN_H_REGSEL: REGSEL page */

/* REGSEL offsets 0x80-0xFF are one-shot operations, not registers. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed only      */

/* Booting a staged image: poll FN_R_BOOT_STATE for FN_BOOT_READY, write
 * FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC, then from console RAM read FN_H_REGSEL +
 * FN_HOT_SWAP and jump to the new image's entry. The read must come from RAM: the
 * swap replaces the whole window. The client numbers its transactions from the cart's
 * FN_R_ACKSEQ + 1, never a counter of its own, since a console reset doesn't reset
 * the cart. */

#include "fuji_mailbox_check.h"

#endif /* FUJI_MAILBOX_H */
