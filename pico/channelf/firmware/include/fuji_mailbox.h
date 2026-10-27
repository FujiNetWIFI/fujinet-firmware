/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Fairchild
 * Channel F. Shared by the RP2040 cart firmware and the MAME device;
 * hand-mirrored in testrom/fujilib.inc.
 *
 * ROMC 05 is a store cycle, so console -> cart is an ordinary write and
 * cart -> console is memory the cart paints. Reads of the register and TX
 * pages have no side effect, and the F8 gives every device the full 16-bit
 * address, so there is no aliasing.
 *
 * The remaining hazard is a stray write: DC0 auto-increments on every LM and
 * ST, so a block copy past the RAM arena writes registers and TX. Never let a
 * DC0 run cross $F800 unintentionally.
 *
 * MEMORY MAP (console addresses; the BIOS owns $0000-$07FF)
 *
 *   $0800-$47FF  16K ROM window   client image, or a booted Videocart;
 *                                 a client's "FUJI" claim sits at $47FC
 *   $4800-$67FF  8K page window   a banked client's selected page (APPBANK);
 *                                 reads $FF for any other image
 *   $6800-$7FFF  unpopulated      reads $FF
 *   $8000-$F7FF  30K RAM          the client's own read/write memory
 *   $F800-$FBFF  1K reply window  cart-painted, read in place
 *   $FC00-$FCFF  status page      cart-painted, plus the free-running tick
 *   $FD00-$FDFF  register writes  address picks the register, data is the value
 *   $FE00-$FEFF  raw REGDATA      kept so fujimail.c compiles verbatim
 *   $FF00-$FFFF  TX stream        a write anywhere in the page appends its data
 *
 * The BIOS enters a cart at $0800 ($55 at $0800, jump to $0802). The 32K
 * arena at $8000 uses the offsets the shared fujimail.c expects; console
 * address = FN_ARENA_BASE + offset.
 *
 * A write anywhere in the TX page appends, so a client sets DC0 once with DCI
 * and runs up to 256 STs. Overrunning wraps DC0 to $0000, BIOS ROM, which
 * ignores writes.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

#include "fuji_mailbox_proto.h"

/* ---------------- address map ---------------- */

#define FN_ROM_BASE      0x0800   /* where the BIOS enters a cartridge       */
#define FN_ROM_SIZE      0x4000   /* 16K client window                       */

#define FN_ARENA_BASE    0x8000   /* RAM + mailbox; offsets below are from here */
#define FN_ARENA_SIZE    0x8000   /* 32K                                     */
#define FN_RAM_TOP       0x7800   /* arena offsets below this are plain RAM  */

/* ---------------- cart -> console: painted memory ---------------- */

/* The whole reply in one 1K slice; FN_R_SLICE_ECHO is kept for fujimail.c
 * and is always 0. */
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
#define FN_PROTO_VER     2        /* 2: APPBANK pages and the tick           */
#define FN_R_SLICE_ECHO  0x7C0C
                                  /* 0x7C0D reserved, reads 0                */
#define FN_R_TICK_LO     0x7C0E   /* free-running 60 Hz count, LE ($FC0E)    */
#define FN_R_TICK_HI     0x7C0F

#define FN_R_PAINT_END   0x7CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* The tick: the console has no frame interrupt a client can use, so the
 * cart paints a 60 Hz count from power-up that keeps running during a
 * transaction. Read HI, LO, HI and retry if the HIs differ. Not painted while
 * the mailbox is dead. Under MAME it stands still across a transaction, which
 * runs inside one store. */

/* "FUJI" at console $47FC marks a FujiNet client, so the mailbox stays live
 * after it boots; without it the mailbox goes dead for the session. Commercial
 * Videocarts are 2K-6K and never reach $47FC. Client images are exactly
 * FN_ROM_SIZE bytes plus whole APPBANK pages; tools/checkrom.py enforces it. */
#define FN_ROM_CLAIM     (FN_ROM_SIZE - 4)   /* image offset; console $47FC   */

/* The reply and RXLEN are repainted only by a SEQ commit or an RXSLICE
 * select, so a client may stream reply bytes straight into the TX page. */

/* ---------------- console -> cart: writes ---------------- */

/* A write of V to FN_H_REGSEL + n (n < 0x80) is one complete register write;
 * the cart synthesizes the REGSEL/REGDATA pair fujimail.c decodes. */
#define FN_H_REGSEL      0x7D00   /* write V to +n: register n = V ($FD00)   */
#define FN_H_REGDATA     0x7E00   /* raw REGDATA half, for the shared decoder */
#define FN_H_DATA        0x7F00   /* write anywhere in page: append data byte */

#define FN_H_PAGE_MASK   0x7F00

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot operations, not registers. */
#define FN_HOT_BANK      0x80     /* +n: map APPBANK page n at $4800         */
#define FN_HOT_BANK_LAST 0xEF
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */

/* A .cfg sibling (FN_STREAM_CFG) is accepted and discarded: there are no mapper variants. */

/* APPBANK: a client bigger than the 16K window. The image is the usual 16K
 * followed by k 8K pages, 1 <= k <= FN_APP_MAX_PAGES; page n is image bytes
 * [FN_APP_FIXED + n*FN_APP_PAGE, +FN_APP_PAGE) and appears at $4800-$67FF.
 *
 * A store of any value to $FD80+n selects page n, decoded inline so the next
 * fetch already comes from it. A missing page reads $FF. A swap maps page 0;
 * a console reset does not, since the BIOS re-enters in the fixed 16K.
 *
 * Banking is decoded only while the mailbox is live, so an image with pages
 * but no claim is refused (FN_BOOT_ERR_NOMAP). Older firmware refuses a banked
 * image with FN_BOOT_ERR_TOOBIG. The page cap is cart SRAM: two slots of the
 * largest image must fit beside the arena and the ring, since core1 may never
 * fault out to flash. */
#define FN_APP_FIXED     0x4000   /* image offset of page 0                  */
#define FN_APP_WIN       0x4800   /* console address of the page window      */
#define FN_APP_PAGE      0x2000   /* 8K                                      */
#define FN_APP_MAX_PAGES 6
#define FN_APP_MAX_SIZE  (FN_APP_FIXED + FN_APP_MAX_PAGES * FN_APP_PAGE) /* 64K */

/* Booting a staged image, from the client's side:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC (one store to $FD11);
 *   3. copy the swap stub into the RAM arena and jump to it;
 *   4. the stub writes any value to $FDFE (FN_H_REGSEL + FN_HOT_SWAP): the
 *      cart flips the ROM window to the staged image before the next fetch;
 *   5. the stub jumps to $0000 and the BIOS cold-starts the new image.
 *
 * The stub runs from the arena because the swap replaces the whole ROM window.
 *
 * The client derives its next sequence number from FN_R_ACKSEQ + 1 (wrapping
 * 255 -> 1; 0 means never used), never a local counter: a console reset or a
 * jump to $0000 restarts the client but not the cart. */

#include "fuji_mailbox_check.h"

#endif /* FUJI_MAILBOX_H */
