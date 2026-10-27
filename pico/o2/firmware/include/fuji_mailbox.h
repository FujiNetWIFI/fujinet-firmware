/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Magnavox Odyssey 2.
 * Shared by the cart firmware and the o2em model; hand-mirrored in o2fuji.inc.
 *
 * console -> cart: MOVX writes to the external-data window (P14 low + ~WR low).
 * cart -> console: MOVP reads of a program-window page the cart rewrites. The
 * connector has no ~RD, so answering a MOVX read can collide on the bus (why
 * Videopac 31/40 hang on a G7400); a ~PSEN read is plain ROM.
 *
 * The Voice decodes writes to $80-$DF and $F0-$FF as sound and $E4/$E8-$EF as
 * bank selects, leaving only $E0-$E3 and $E5-$E7 safe, hence the indexed
 * register pair. (PicoPAC's $FE/$FF handshake garbles with a Voice attached.)
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
#define FN_REG_CODEBANK 0x08  /* APPBANK: the image page mapped at $800-$F1F     */
#define FN_REG_DATABANK 0x09  /* reserved                                        */
#define FN_REG_POKE_LO  0x0A  /* APPBANK: write pointer into $400-$7FF, low byte */
#define FN_REG_POKE_HI  0x0B  /*          ... and its high bits (offset 0-$3FF)  */
#define FN_REG_POKE     0x0C  /* APPBANK: store at the pointer, then advance it  */
#define FN_REG_RXWIN    0x0D  /* APPBANK: the read window starts at reply byte
                               *          value * 16                              */

#define FN_BOOTSEL_MAGIC 0xB5 /* exact value required, so a stray write is inert */

/* ---------- protocol 2: APPBANK images ----------
 *
 *     file offset 0      $400-$7FF   the fixed half: always mapped
 *     1024 + 2048*n      $800-$FFF   page n, mapped at $800-$F1F when selected
 *
 * with N = 2..O2_APP_MAXPAGES pages. The fixed half must be fixed because an
 * interrupt holds A11 at 0 for its whole service routine. It is cartridge RAM,
 * writable through FN_REG_POKE, so interrupt code can keep MOVP tables there.
 *
 * $F00-$F1F belongs to the selected page; $F20-$FFF is the mailbox, with
 * $F30-$FFF showing the reply from byte FN_REG_RXWIN*16 (or FN_REG_RXSLICE*208).
 * CODEBANK, POKE, RXWIN and RXSLICE take effect before the console's next fetch.
 *
 * Page 0 carries the FUJI claim at $F2C and this header at $F30:
 *     'A' 'P' 'P' 'B', version (1), N
 */
#define O2_APP_FIXED_BYTES 1024
#define O2_APP_PAGE_BYTES  2048
#define O2_APP_PAGE_BASE   0x800      /* console address of a page's first byte */
#define O2_APP_PAGE_END    0xF20      /* first console address that is not      */
#define O2_APP_MAXPAGES    31         /* 1K + 31 x 2K = 63K of cartridge RAM    */
#define O2_APP_HDR         0xF30      /* in page 0                              */
#define O2_APP_HDR_SIG     "APPB"
#define O2_APP_VERSION     1
#define O2_APP_RXWIN_UNIT  16

/* The FN_W_DATA stream is, in order:
 *     NPARAM x { size byte (1|2|4), then that many value bytes, little-endian }
 *     then TXLEN raw payload bytes
 */
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
#define FN_R_PROTO_VER  0xF2B /* FN_PROTO_VER                                    */
#define FN_PROTO_VER    2     /* 2: APPBANK images (above)                       */

/* The cart never publishes over $F2C-$F2F. An image with FN_R_CLAIM_SIG there
 * promises $F20-$FFF is free for the mailbox and $E0-$E3 does not drive The
 * Voice; only then does the mailbox survive its boot. */
#define FN_R_CLAIM      0xF2C
#define FN_R_CLAIM_LEN  4
#define FN_R_CLAIM_SIG  "FUJI"

#define FN_R_DATA       0xF30 /* reply slice selected by FN_REG_RXSLICE (RXWIN)   */
#define FN_R_SLICE_LEN  0xD0  /* 208 bytes: $F30..$FFF                           */

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2      /* image staged; takes over on the next reset */
#define FN_BOOT_FAILED   0x80

/* FN_R_BOOT_ERR values. */
#define FN_BOOT_ERR_TOOBIG    1
#define FN_BOOT_ERR_TRUNCATED 2
#define FN_BOOT_ERR_NOMAP     3
#define FN_BOOT_ERR_BUSY      4   /* an APPBANK image is live: power-cycle first */

/* FN_R_ERR values; mirrors fb_status_t in fujibus.h. */
#define FN_ERR_OK        0
#define FN_ERR_NOLINK    1
#define FN_ERR_TIMEOUT   2
#define FN_ERR_BADFRAME  3
#define FN_ERR_TOOBIG    4

/* The client must take its next sequence number from FN_R_ACKSEQ + 1, never a
 * local counter: a console RESET does not reset the cart, so a restarted counter
 * repeats an acknowledged value and the request is silently ignored. Sequence 0
 * means "never used". */

#endif /* FUJI_MAILBOX_H */
