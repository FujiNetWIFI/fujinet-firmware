/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Nintendo
 * Entertainment System.
 *
 * Single source of truth, shared by the RP2354 cart firmware, the MAME cart
 * device model, and (hand-mirrored) the 6502 console clients in
 * testrom/fujinet.inc and fujinet-lib's nes target.
 *
 * WHAT IS DIFFERENT HERE
 *
 * The NES cart edge carries a real R/W line and a full address bus (A15 only
 * as /ROMSEL), so this is the Channel F protocol family: console -> cart is an
 * ordinary WRITE to a hotspot page, cart -> console is memory the cart paints.
 * Reads of the hotspot pages are inert, and there is no address aliasing --
 * the cart decodes $5000-$5FFF exactly.
 *
 * The hazard that remains is the 6502's read-modify-write dummy write: every
 * RMW instruction (INC/DEC/ASL/LSR/ROL/ROR and the illegal SLO/RLA/SRE/RRA/
 * DCP/ISC) writes the OLD value back before writing the new one, on
 * consecutive bus cycles at one address. On a page where any write is an
 * event, that lands as a spurious event. Client rule: only STA/STX/STY to
 * these pages -- plain or indexed, since an indexed store's extra cycle is a
 * READ and reads are inert here. tools/checkrom.py rejects the RMW opcodes
 * statically; the cart also recognises the exact signature (two writes, one
 * address, adjacent cycles), drops the dummy, and counts it in FN_R_DIAG_RMW.
 *
 * MEMORY MAP (console addresses)
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
 * Everything the cart serves out of its own memory in $5000-$5FFF is a 4K
 * "arena" at FN_ARENA_BASE whose OFFSETS below keep the sibling ports' page
 * shape -- reply, status, REGSEL, REGDATA, DATA, one 256-byte page each --
 * so that fujimail.c is copied across unchanged. Console address =
 * FN_ARENA_BASE + offset.
 *
 * THE TX PAGE. A write ANYWHERE in $5700-$57FF appends its data byte, so a
 * client can `STA $5700` (or STA $5700,X) for every byte with no address
 * arithmetic.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* ---------------- address map ---------------- */

#define FN_ARENA_BASE    0x5000   /* mailbox + loader; offsets below are from here */
#define FN_ARENA_SIZE    0x1000   /* 4K                                      */

#define FN_WRAM_BASE     0x6000   /* cart-served PRG-RAM                     */
#define FN_WRAM_SIZE     0x2000   /* 8K in the window; banked boards later   */

#define FN_VECTOR_BASE   0xFF00   /* served by the cart only while SRAM_EN=0 */

/* ---------------- cart -> console: painted memory ---------------- */

/* The whole reply in one piece at $5000-$53FF: a client reads directory
 * entries straight out of the window and streams them back into the next
 * transaction with no bounce buffer. FN_R_SLICE_ECHO stays in the register
 * file so fujimail.c compiles verbatim (with one slice it is always 0).
 * The loader borrows the same window for 1K image slices. */
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

/* The loader's half of the status page: what the 6502 copies next and where.
 * Published in this order by the cart -- DST, OFF, then SEQ last -- so a
 * client that sees a new SEQ knows the slice behind it is whole. */
#define FN_R_LOAD_STATE  0x40D    /* FN_LOAD_*                               */
#define FN_R_LOAD_DST    0x40E    /* FN_LOAD_DST_PRG / _CHR                  */
#define FN_R_LOAD_OFF    0x40F    /* 1K index inside the current window      */
#define FN_R_LOAD_SEQ    0x410    /* bumps once per painted slice, 1..255    */
#define FN_R_LOAD_PCT    0x411    /* 0-100 across the whole image            */
#define FN_R_SRAM_STATE  0x412    /* mirrors the '595 SRAM_EN bit once flushed */
#define FN_R_DIAG_RMW    0x413    /* count of RMW dummy writes dropped       */
#define FN_R_MAPPER      0x414    /* mapper number of the live image, low byte */
#define FN_R_LINK        0x415    /* 1 once the ESP32 link has been seen      */

#define FN_R_PAINT_END   0x4FC    /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* A FujiNet client declares itself with "FUJI" in the last 16 bytes of its
 * PRG image -- console $FFF0-$FFF3 in the fixed bank, just below the vectors.
 * An image carrying it keeps the mailbox alive after it boots; a commercial
 * game does not, and the mailbox goes dead for the session (the cart stops
 * answering $5000-$57FF; the loader ROM at $5800 stays). The offset is from
 * the END of PRG so it is the same for every PRG size and every mapper whose
 * last bank is fixed, which is all of them. */
#define FN_PRG_CLAIM_FROM_END 16
#define FN_R_CLAIM_LEN   4
#define FN_R_CLAIM_SIG   "FUJI"

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* Reply-window stability invariant: the reply and RXLEN are repainted ONLY by
 * a SEQ commit, an RXSLICE select, or -- while the loader runs -- a LOAD_SEQ
 * bump. Between those a client may stream bytes straight out of the reply
 * window into the TX page. */

/* ---------------- console -> cart: writes ---------------- */

/* A write of value V to FN_H_REGSEL + n (n < 0x80) is one complete register
 * write: the cart synthesises the REGSEL/REGDATA pair that fujimail.c decodes.
 * The pair survives in the protocol because keeping it is free and it lets
 * fujimail.c stay byte-identical to the sibling ports. */
#define FN_H_REGSEL      0x500    /* write V to +n: register n = V ($5500)   */
#define FN_H_REGDATA     0x600    /* raw REGDATA half, for the shared decoder */
#define FN_H_DATA        0x700    /* write anywhere in page: append data byte */

#define FN_H_PAGE_MASK   0xF00

#define FN_LOADER        0x800    /* the 2K loader ROM ($5800-$5FFF)         */
#define FN_LOADER_SIZE   0x800

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot special operations rather than
 * register numbers, in the bit7-set half as on every sibling. */
#define FN_HOT_SWAP      0xFE     /* begin loading the staged image; armed-only */

/* Registers, reached by writing to FN_H_REGSEL + n.
 * 0x00-0x13 mirrors the O2/Astrocade/Arcadia/Coleco/Channel F numbering. */
#define FN_REG_DEVICE    0x00     /* FujiBus device id, e.g. 0x70            */
#define FN_REG_CMD       0x01     /* FujiBus command id                      */
#define FN_REG_NPARAM    0x02     /* number of parameters in the TX stream   */
#define FN_REG_DATA_RST  0x05     /* any value: rewind the TX write pointer  */
#define FN_REG_RXSLICE   0x06     /* which reply slice FN_R_DATA shows       */
#define FN_REG_SEQ       0x10     /* nonzero, != ACKSEQ: launch transaction  */
#define FN_REG_BOOTLOCK  0x11     /* FN_BOOTLOCK_MAGIC: arm the image load   */
#define FN_REG_BOOTSEL_1 0x12     /* FN_BOOTSEL_MAGIC1, then...              */
#define FN_REG_BOOTSEL_2 0x13     /* ...FN_BOOTSEL_MAGIC2: reboot to BOOTSEL */
#define FN_REG_SLICE_ACK 0x14     /* loader: slice LOAD_SEQ has been copied  */

#define FN_BOOTLOCK_MAGIC 0xB5
#define FN_BOOTSEL_MAGIC1 0xB5
#define FN_BOOTSEL_MAGIC2 0x4A

/* The FN_H_DATA stream is, in order:
 *     NPARAM x { size byte (1|2|4), then that many value bytes, little-endian }
 *     then the raw payload
 * Identical to every sibling port. */
#define FN_TX_MAX        320

/* DBC push stream ids, matching lib/media/rs232/diskTypeROM.cpp. Stream 1 is
 * the optional .cfg sibling; a .nes file carries its own header, so it is
 * accepted and discarded. */
#define FN_STREAM_ROM    0
#define FN_STREAM_CFG    1

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2        /* image staged; arm BOOTLOCK, then JMP $5800 */
#define FN_BOOT_FAILED   0x80

/* FN_R_BOOT_ERR values. */
#define FN_BOOT_ERR_TOOBIG    1
#define FN_BOOT_ERR_TRUNCATED 2
#define FN_BOOT_ERR_NOMAP     3
#define FN_BOOT_ERR_STOREBUSY 4

/* FN_R_ERR values; mirrors fb_status_t in fujibus.h. */
#define FN_ERR_OK        0
#define FN_ERR_NOLINK    1
#define FN_ERR_TIMEOUT   2
#define FN_ERR_BADFRAME  3
#define FN_ERR_TOOBIG    4

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
 * The loader ROM at $5800 is the swap stub: it is cart-served and untouched
 * by SRAM writes, so unlike every sibling nothing has to be copied into
 * console RAM first. It stores to FN_H_REGSEL + FN_HOT_SWAP, and from then on
 * follows FN_R_LOAD_STATE/DST/OFF/SEQ slice by slice, acking each with
 * FN_REG_SLICE_ACK, until FN_LOAD_DONE, then JMP ($FFFC) into the new image.
 * Power-on is the same loader, entered through the cart-served reset vector
 * while SRAM_EN=0, pulling CONFIG out of the cart's flash by the same path.
 *
 * CRITICAL, inherited from every sibling bring-up: the client derives its next
 * sequence number from the cart's own persisted FN_R_ACKSEQ + 1 (wrapping
 * 255 -> 1; 0 is reserved as "never used"), never from a program-local
 * counter. A console RESET restarts the client and re-zeroes its variables
 * but does NOT reset the cart: there is no reset line on this edge. */

#endif /* FUJI_MAILBOX_H */
