/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Magnavox Odyssey 2.
 *
 * Single source of truth, shared by the RP2040 cart firmware, the o2em model,
 * and (hand-mirrored) the 8048 console client in o2fuji.inc.
 *
 * The two directions do NOT use the same mechanism, because the O2 cartridge
 * port is asymmetric:
 *
 *   console -> cart   MOVX writes into the external-data window. Fully strobed
 *                     (the cart decodes P14 low + ~WR low), unambiguous, and
 *                     already proven by shipping carts.
 *
 *   cart -> console   reads out of the cartridge PROGRAM window via MOVP. The
 *                     connector carries no ~RD, so a cart answering a MOVX read
 *                     has to drive the bus off a level condition and can collide
 *                     (this is why Videopac 31/40 hang on a G7400). A ~PSEN read
 *                     has no such hazard: it is just ROM, which the cart already
 *                     has to serve. So the reply lives in a page of the ROM
 *                     window that the cart rewrites between transactions.
 *
 * Address choice matters more than it looks: The Voice decodes writes to
 * $80-$DF and $F0-$FF as sound triggers, and $E4/$E8-$EF as bank selects. The
 * only Voice-safe addresses in the whole window are $E0-$E3 and $E5-$E7, so the
 * write side is an indexed register pair rather than a flat file. PicoPAC's
 * menu handshake uses $FE/$FF and that is exactly why it garbles with a Voice
 * attached (its issues #1 and #5).
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* ---------- console -> cart: MOVX writes, external-data window ---------- */

#define FN_W_REGSEL     0xE0  /* select the register the next $E1 write targets */
#define FN_W_REGDATA    0xE1  /* value for the selected register                */
#define FN_W_DATA       0xE2  /* auto-incrementing TX byte stream (see below)   */
#define FN_W_SEQ        0xE3  /* written LAST; launches the transaction         */

/* Registers reached through FN_W_REGSEL / FN_W_REGDATA. */
#define FN_REG_DEVICE   0x00  /* FujiBus device id, e.g. 0x70                   */
#define FN_REG_CMD      0x01  /* FujiBus command id                             */
#define FN_REG_NPARAM   0x02  /* number of parameters in the TX stream (0-8)     */
#define FN_REG_TXLEN_LO 0x03  /* raw payload length after the params, LE         */
#define FN_REG_TXLEN_HI 0x04
#define FN_REG_DATA_RST 0x05  /* any value: rewind the FN_W_DATA write pointer   */
#define FN_REG_RXSLICE  0x06  /* which slice of the reply the read window shows  */
#define FN_REG_BOOTSEL  0x07  /* magic FN_BOOTSEL_MAGIC: reboot cart into BOOTSEL*/
#define FN_REG_CODEBANK 0x08  /* MegaCart-compatible code bank                   */
#define FN_REG_DATABANK 0x09  /* MegaCart-compatible data bank                   */

#define FN_BOOTSEL_MAGIC 0xB5 /* exact value required, so a stray write is inert */

/* The FN_W_DATA stream is, in order:
 *     NPARAM x { size byte (1|2|4), then that many value bytes, little-endian }
 *     then TXLEN raw payload bytes
 * A stream rather than the Intellivision's FN_PARAM_SIZE[8]/FN_PARAM_VAL[8][4]
 * tables, which would cost 40 addresses this window does not have. */
#define FN_TX_MAX       320   /* 8 params x 5 bytes + a 256-byte payload + slack */

/* ---------- cart -> console: MOVP reads, program window page $F00 ---------- */

#define FN_R_PAGE       0xF00 /* the whole page the cart owns                    */
#define FN_R_STUB_END   0xF20 /* $F00-$F1F is the client's own reader stub       */

#define FN_R_ACKSEQ     0xF20 /* echoes FN_W_SEQ when the reply is ready         */
#define FN_R_STATUS     0xF21 /* bit0 link up, bit1 busy                         */
#define FN_R_ERR        0xF22 /* fb_status_t of the last transaction             */
#define FN_R_REPLY_CMD  0xF23 /* 0x06 ACK / 0x15 NAK                             */
#define FN_R_RXLEN_LO   0xF24 /* total reply length, LE (may exceed one slice)   */
#define FN_R_RXLEN_HI   0xF25
#define FN_R_BOOT_STATE 0xF26
#define FN_R_BOOT_PCT   0xF27 /* 0-100, for a MOUNT_IMAGE progress bar           */
#define FN_R_BOOT_ERR   0xF28
#define FN_R_MAGIC0     0xF29 /* 'F' -- cart presence check                      */
#define FN_R_MAGIC1     0xF2A /* 'N'                                             */
#define FN_R_PROTO_VER  0xF2B

/* $F2C-$F2F is the only part of the page the cart never publishes over, so it
 * is where a cartridge image declares itself. An image carrying FN_R_CLAIM_SIG
 * there promises two things: $F20-$FFF is reserved for the mailbox, and $E0-$E3
 * is not used to drive The Voice. Both have to hold before the mailbox can
 * survive a boot -- an ordinary game keeps its code in this page and does write
 * The Voice, which is why booting one disables the mailbox for the session. */
#define FN_R_CLAIM      0xF2C
#define FN_R_CLAIM_LEN  4
#define FN_R_CLAIM_SIG  "FUJI"

#define FN_R_DATA       0xF30 /* reply slice selected by FN_REG_RXSLICE          */
#define FN_R_SLICE_LEN  0xD0  /* 208 bytes: $F30..$FFF                           */

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* FN_R_ERR values; mirrors fb_status_t in fujibus.h. */
/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2      /* image staged; takes over on the next reset */
#define FN_BOOT_FAILED   0x80

/* FN_R_BOOT_ERR values. */
#define FN_BOOT_ERR_TOOBIG    1
#define FN_BOOT_ERR_TRUNCATED 2
#define FN_BOOT_ERR_NOMAP     3

#define FN_ERR_OK        0
#define FN_ERR_NOLINK    1
#define FN_ERR_TIMEOUT   2
#define FN_ERR_BADFRAME  3
#define FN_ERR_TOOBIG    4

/* CRITICAL: the client must derive its next sequence number from the cart's own
 * persisted FN_R_ACKSEQ + 1, never from a program-local counter. A console RESET
 * restarts the client and re-zeroes its variables but does NOT reset the cart, so
 * a local counter recomputes a value the cart has already acknowledged and no
 * further request is ever sent -- while the screen keeps showing the first
 * reply, looking like a stable success. This cost real debugging time on the
 * Intellivision. Sequence 0 is reserved as "never used". */

#endif /* FUJI_MAILBOX_H */
