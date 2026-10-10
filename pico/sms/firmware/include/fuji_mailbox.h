/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Sega Master
 * System. Shared by the RP2354B firmware, the MAME device and the host tests;
 * testrom/fujilib.h and fujinet-lib's sms target mirror it by hand.
 *
 * The edge has /WR, so console -> cart is a WRITE to a hotspot page and reads
 * of those pages are inert. The 4K arena keeps the NES offsets, so fujimail.c
 * is shared unchanged; only the base moves, to the top 4K of slot 2:
 *
 *   $B000-$B3FF  1K reply window   cart-painted, read in place
 *   $B400-$B4FF  status page       cart-painted
 *   $B500-$B5FF  register writes   address picks the register, data is the value
 *   $B600-$B6FF  raw REGDATA       kept so fujimail.c compiles verbatim
 *   $B700-$B7FF  TX stream         a write anywhere in the page appends its data
 *   $B800-$BFFF  loader page       2K of cart-served Z80 code
 *
 * The arena is cart-served in both modes: RESIDENT (CONFIG, all of
 * $0000-$BFFF from the cart) and APP (a claimed image in the SRAM).
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* ---------------- address map ---------------- */

#define FN_ARENA_BASE    0xB000   /* mailbox + loader; offsets below are from here */
#define FN_ARENA_SIZE    0x1000   /* 4K                                      */

#define FN_LOADWIN_BASE  0x8000   /* RESIDENT: the 8K window the loader reads */
#define FN_LOADWIN_SIZE  0x2000

#define FN_RESIDENT_MAX  0x8000   /* CONFIG is served at $0000-$7FFF         */

/* ---------------- cart -> console: painted memory ---------------- */

/* One 1K slice; FN_R_SLICE_ECHO stays so fujimail.c compiles verbatim. */
#define FN_R_DATA        0x000    /* the whole reply ($B000)                 */
#define FN_R_SLICE_LEN   0x400
#define FN_R_NSLICES     1        /* x 1024 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x400    /* first painted status byte ($B400)       */
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

/* The loader's half. SEQ is published last, after the window is mapped. */
#define FN_R_LOAD_STATE  0x40D    /* FN_LOAD_*                               */
#define FN_R_LOAD_K      0x40E    /* 8K SRAM bank now behind the load window */
#define FN_R_LOAD_N      0x40F    /* windows in this load                    */
#define FN_R_LOAD_SEQ    0x410    /* bumps once per window, 1..255           */
#define FN_R_LOAD_PCT    0x411    /* 0-100 across the whole load             */
#define FN_R_MODE        0x412    /* FN_MODE_*: what the cart is serving     */
#define FN_R_MAPPER      0x414    /* SMSMAP_* of the live image              */
#define FN_R_LINK        0x415    /* 1 once the ESP32 link has been seen     */
#define FN_R_BOOT_GOT0   0x416    /* image bytes received so far, 24-bit LE  */
#define FN_R_BOOT_TOT0   0x419    /* image size declared at OPEN, 24-bit LE  */

/* Restored by the loader before JP $0000: what the BIOS left behind (snooped
 * at power-on) and the new image's mapper RAM mirror. An image with no Sega
 * header, which no export BIOS boots, gets RAM filled with $F0 (as on MAME's
 * Japanese and Korean consoles) and power-on VDP registers; games still read
 * their port $3E value from $C000. Valid at FN_LOAD_DONE. */
#define FN_R_HO_C000     0x420    /* the BIOS's $C000 byte                   */
#define FN_R_HO_3E       0x421    /* port $3E (memory control)               */
#define FN_R_HO_3F       0x422    /* port $3F (I/O control)                  */
#define FN_R_HO_VDP      0x423    /* VDP registers 0-10                      */
#define FN_HO_VDP_REGS   11
#define FN_R_HO_MIRROR   0x42E    /* $DFFC-$DFFF: the mapper registers' RAM copy */
#define FN_R_HO_FILL     0x432    /* console RAM is filled with this first   */

#define FN_R_PAINT_END   0x4FC    /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* "FUJI" at image offset $7FDC (below the SDSC header, inside the BIOS
 * checksum range) keeps the mailbox live after the image boots. */
#define FN_CLAIM_OFFSET  0x7FDC
#define FN_R_CLAIM_LEN   4
#define FN_R_CLAIM_SIG   "FUJI"

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* The reply is repainted only by a SEQ commit or an RXSLICE select, so a
 * client may stream bytes from it straight into the TX page. */

/* ---------------- console -> cart: writes ---------------- */

/* Writing V to FN_H_REGSEL + n (n < 0x80) sets register n = V; the cart
 * synthesises the REGSEL/REGDATA pair fujimail.c decodes. */
#define FN_H_REGSEL      0x500    /* write V to +n: register n = V ($B500)   */
#define FN_H_REGDATA     0x600    /* raw REGDATA half, for the shared decoder */
#define FN_H_DATA        0x700    /* write anywhere in page: append data byte */

#define FN_H_PAGE_MASK   0xF00

#define FN_LOADER        0x800    /* the 2K loader page ($B800-$BFFF)        */
#define FN_LOADER_SIZE   0x800

/* Loader entry points. Jump, don't call: neither returns. */
#define FN_LOADER_BOOT   0xB800   /* load the armed image and start it       */
#define FN_LOADER_CONFIG 0xB803   /* restart CONFIG                          */

/* FN_H_REGSEL + 0x80..0xFF are one-shot operations, not registers. */
#define FN_HOT_SWAP      0xFE     /* back to RESIDENT; load the armed image, or FAILED */
#define FN_HOT_GO        0xFD     /* flip to the loaded image on the next fetch of $0000 */
#define FN_HOT_CONFIG    0xFC     /* hand the console back to CONFIG         */

/* Registers; the numbering matches every sibling. */
#define FN_REG_DEVICE    0x00     /* FujiBus device id, e.g. 0x70            */
#define FN_REG_CMD       0x01     /* FujiBus command id                      */
#define FN_REG_NPARAM    0x02     /* number of parameters in the TX stream   */
#define FN_REG_DATA_RST  0x05     /* any value: rewind the TX write pointer  */
#define FN_REG_RXSLICE   0x06     /* which reply slice FN_R_DATA shows       */
#define FN_REG_SEQ       0x10     /* nonzero, != ACKSEQ: launch transaction  */
#define FN_REG_BOOTLOCK  0x11     /* FN_BOOTLOCK_MAGIC: arm the image load   */
#define FN_REG_BOOTSEL_1 0x12     /* FN_BOOTSEL_MAGIC1, then...              */
#define FN_REG_BOOTSEL_2 0x13     /* ...FN_BOOTSEL_MAGIC2: reboot to BOOTSEL */
#define FN_REG_SLICE_ACK 0x14     /* loader: window LOAD_SEQ has been read   */

#define FN_BOOTLOCK_MAGIC 0xB5
#define FN_BOOTSEL_MAGIC1 0xB5
#define FN_BOOTSEL_MAGIC2 0x4A

/* TX stream: NPARAM x {size 1|2|4, value LE}, then the payload. */
#define FN_TX_MAX        320

/* DBC push streams (lib/device/rs232/disk.cpp); a .cfg `mapper=` line
 * overrides the cart's mapper choice. */
#define FN_STREAM_ROM    0
#define FN_STREAM_CFG    1

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2        /* image staged; arm BOOTLOCK, then JP $B800 */
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
#define FN_LOAD_WINDOW   1        /* window LOAD_K is mapped: read all 8K, ack */
#define FN_LOAD_DONE     2        /* image in place: restore, GO, JP $0000   */
#define FN_LOAD_FAILED   0x80     /* nothing armed: the loader returns to CONFIG */

/* FN_R_MODE values. */
#define FN_MODE_RESIDENT 0        /* CONFIG, served by the cart              */
#define FN_MODE_GAME     1        /* an image in the SRAM, mailbox off       */
#define FN_MODE_APP      2        /* an image in the SRAM that claims the mailbox */

/* Booting: poll FN_R_BOOT_STATE for READY, write FN_REG_BOOTLOCK =
 * FN_BOOTLOCK_MAGIC, then DI and JP FN_LOADER_BOOT. The client's next SEQ is
 * always FN_R_ACKSEQ + 1 (255 wraps to 1), never a local counter: the SMS1
 * Reset button restarts the client but not the cart. */

#endif /* FUJI_MAILBOX_H */
