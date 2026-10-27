/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the NES. Shared by
 * the cart firmware and the MAME device; mirrored by hand in
 * testrom/fujinet.inc and fujinet-lib's nes target.
 *
 * The edge has R/W, so console -> cart is a write to a hotspot page; reads of
 * those pages are inert. A 6502 RMW instruction writes the old value back on
 * the cycle before the new one, a spurious event, so clients use only
 * STA/STX/STY there. tools/checkrom.py rejects RMW opcodes; the cart also drops
 * a dummy write (two writes, one address, adjacent cycles) and counts it in
 * FN_R_DIAG_RMW.
 *
 * Memory map (console addresses):
 *
 *   $4020-$4FFF  open bus (mapper registers for a few boards; not ours)
 *   $5000-$53FF  1K reply window   cart-painted, read in place
 *   $5400-$54FF  status page       cart-painted
 *   $5500-$55FF  register writes   address picks the register, data is the value
 *   $5600-$56FF  raw REGDATA       kept so fujimail.c compiles verbatim
 *   $5700-$57FF  TX stream         a write anywhere in the page appends its data
 *   $5800-$5FFF  loader ROM        2K, cart-served; the swap stub for everyone
 *   $6000-$7FFF  WRAM              8K+, cart-served (mapper-gated)
 *   $8000-$FFFF  PRG               the external SRAM through the 4 x 8K PIO table
 *
 * The offsets below are from FN_ARENA_BASE and keep the shared fujimail.c
 * page shape (reply, status, REGSEL, REGDATA, DATA).
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

#include "fuji_mailbox_proto.h"

/* ---------------- address map ---------------- */

#define FN_ARENA_BASE    0x5000   /* mailbox + loader; offsets below are from here */
#define FN_ARENA_SIZE    0x1000   /* 4K                                      */

#define FN_WRAM_BASE     0x6000   /* cart-served PRG-RAM                     */
#define FN_WRAM_SIZE     0x2000   /* 8K in the window; banked boards later   */

#define FN_VECTOR_BASE   0xFF00   /* served by the cart only while SRAM_EN=0 */

/* ---------------- cart -> console: painted memory ---------------- */

/* The whole reply in one 1K slice, so FN_R_SLICE_ECHO is always 0. The loader
 * uses the same window for 1K image slices. */
#define FN_R_DATA        0x000    /* the whole reply ($5000)                 */
#define FN_R_SLICE_LEN   0x400
#define FN_R_NSLICES     1        /* x 1024 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x400    /* first painted status byte ($5400)       */
#define FN_R_ACKSEQ      0x400    /* echoes SEQ when the reply is ready      */
#define FN_R_STATUS      0x401    /* bit0 link up, bit1 busy                 */
#define FN_R_ERR         0x402    /* fb_status_t of the last transaction     */
#define FN_R_REPLY_CMD   0x403    /* 0x06 ACK / 0x15 NAK                     */
#define FN_R_RXLEN_LO    0x404    /* total reply length, LE                  */
#define FN_R_RXLEN_HI    0x405
#define FN_R_BOOT_STATE  0x406
#define FN_R_BOOT_PCT    0x407    /* 0-100, for a MOUNT_IMAGE progress bar   */
#define FN_R_BOOT_ERR    0x408
#define FN_R_MAGIC0      0x409    /* 'F' -- cart presence check              */
#define FN_R_MAGIC1      0x40A    /* 'N'                                     */
#define FN_R_PROTO_VER   0x40B
#define FN_PROTO_VER     1
#define FN_R_SLICE_ECHO  0x40C

/* The loader's status. The cart publishes DST, OFF, then SEQ last, so a new
 * SEQ means the slice is whole. */
#define FN_R_LOAD_STATE  0x40D    /* FN_LOAD_*                               */
#define FN_R_LOAD_DST    0x40E    /* FN_LOAD_DST_PRG / _CHR                  */
#define FN_R_LOAD_OFF    0x40F    /* 1K index inside the current window      */
#define FN_R_LOAD_SEQ    0x410    /* bumps once per painted slice, 1..255    */
#define FN_R_LOAD_PCT    0x411    /* 0-100 across the whole image            */
#define FN_R_SRAM_STATE  0x412    /* mirrors the '595 SRAM_EN bit once flushed */
#define FN_R_DIAG_RMW    0x413    /* count of RMW dummy writes dropped       */
#define FN_R_MAPPER      0x414    /* mapper number of the live image, low byte */
#define FN_R_LINK        0x415    /* 1 once the ESP32 link has been seen      */
#define FN_R_BOOT_GOT0   0x416    /* image bytes received so far, 24-bit LE   */
#define FN_R_BOOT_TOT0   0x419    /* image size declared at OPEN, 24-bit LE   */

#define FN_R_PAINT_END   0x4FC    /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* "FUJI" 16 bytes from the end of PRG ($FFF0 in the fixed last bank) keeps the
 * mailbox alive after boot; without it $5000-$57FF go dead for the session
 * (the loader ROM at $5800 stays). */
#define FN_PRG_CLAIM_FROM_END 16

/* The reply and RXLEN are repainted only by a SEQ commit, an RXSLICE select or
 * a LOAD_SEQ bump, so a client may stream the window straight into TX. */

/* ---------------- console -> cart: writes ---------------- */

/* A write of V to FN_H_REGSEL + n (n < 0x80) is a whole register write; the
 * cart expands it into the REGSEL/REGDATA pair fujimail.c decodes. */
#define FN_H_REGSEL      0x500    /* write V to +n: register n = V ($5500)   */
#define FN_H_REGDATA     0x600    /* raw REGDATA half, for the shared decoder */
#define FN_H_DATA        0x700    /* write anywhere in page: append data byte */

#define FN_H_PAGE_MASK   0xF00

#define FN_LOADER        0x800    /* the 2K loader ROM ($5800-$5FFF)         */
#define FN_LOADER_SIZE   0x800

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot operations, not registers. */
#define FN_HOT_SWAP      0xFE     /* begin loading the staged image; armed-only */

/* Platform registers, 0x14 up; the protocol's are in fuji_mailbox_proto.h. */
#define FN_REG_SLICE_ACK 0x14     /* loader: slice LOAD_SEQ has been copied  */

/* A .cfg sibling (FN_STREAM_CFG) is discarded: a .nes carries its own header. */

/* FN_R_LOAD_STATE values. */
#define FN_LOAD_IDLE     0
#define FN_LOAD_SLICE    1        /* a slice is in the window: copy it, ack   */
#define FN_LOAD_DONE     2        /* image in place, mapper reset: JMP ($FFFC) */
#define FN_LOAD_FAILED   0x80

/* FN_R_LOAD_DST values. */
#define FN_LOAD_DST_PRG  0        /* copy to $8000 + OFF*1024               */
#define FN_LOAD_DST_CHR  1        /* copy through $2006/$2007 at OFF*1024    */

/* Booting a staged image, from the client's side:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC (one store to $5511);
 *   3. SEI, NMI off ($2000 = 0), rendering off ($2001 = 0), JMP $5800.
 *
 * The cart-served loader then stores to FN_H_REGSEL + FN_HOT_SWAP, copies
 * slice by slice per FN_R_LOAD_*, acking each with FN_REG_SLICE_ACK, and on
 * FN_LOAD_DONE does JMP ($FFFC). Power-on enters the same loader through the
 * cart-served reset vector while SRAM_EN=0, copying the baked CONFIG.
 *
 * The next sequence number is FN_R_ACKSEQ + 1 (255 wraps to 1; 0 is never
 * used), never a RAM counter: a console Reset does not reset the cart. */

#include "fuji_mailbox_check.h"

#endif /* FUJI_MAILBOX_H */
