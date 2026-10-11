/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Atari 7800.
 * Shared by the RP2354B firmware, the MAME device and the host tests;
 * testrom/fujinet.inc and fujinet-lib's atari7800 target mirror it by hand.
 *
 * The arena sits below $4000, in space the console leaves to the cartridge,
 * so a FujiNet app keeps all of $4000-$FFFF. It keeps the family's arena
 * offsets from $0800, so fujimail.c needs no port changes, and puts the
 * loader below them:
 *
 *   $0600-$07FF  loader          512 bytes of cart-served 6502 code
 *   $0800-$0BFF  1K reply window cart-painted, read in place
 *   $0C00-$0CFF  status page     cart-painted
 *   $0D00-$0DFF  register writes address picks the register, data is the value
 *   $0E00-$0EFF  unused          the define keeps fujimail.c's decoder unchanged
 *   $0F00-$0FFF  TX stream       a write anywhere in the page appends its data
 *
 * The edge has R/W, so console -> cart is a WRITE and reads of the write
 * pages are inert. Never use a read-modify-write instruction on them: the
 * 6502 writes the old value first.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* ---------------- address map ---------------- */

#define FN_ARENA_BASE    0x0800   /* reply..TX; offsets below are from here  */
#define FN_ARENA_SIZE    0x0800   /* 2K                                      */
#define FN_LOADER_BASE   0x0600   /* the loader, just below the arena        */
#define FN_LOADER_SIZE   0x0200

/* Power-on: the cart serves a signed 4K boot block at $F000-$FFFF until the
 * first image is in the SRAM. Its reset vector jumps to FN_LOADER_BOOT. */
#define FN_BOOTBLK_BASE  0xF000
#define FN_BOOTBLK_SIZE  0x1000

/* The loader copies each 1K slice into this 8K window; the cart points the
 * window at the SRAM page being filled. */
#define FN_LOADWIN_BASE  0x4000
#define FN_LOADWIN_SIZE  0x2000

/* ---------------- cart -> console: painted memory ---------------- */

#define FN_R_DATA        0x000    /* the 1K reply ($0800)                    */
#define FN_R_SLICE_LEN   0x400
#define FN_R_NSLICES     1        /* x 1024 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x400    /* first painted status byte ($0C00)       */
#define FN_R_ACKSEQ      0x400    /* echoes SEQ when the reply is ready      */
#define FN_R_STATUS      0x401    /* bit0 link up                            */
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

/* The loader's half: each slice is painted and its window mapped before SEQ changes. */
#define FN_R_LOAD_STATE  0x40D    /* FN_LOAD_*                               */
#define FN_R_LOAD_DST    0x40E    /* high byte of where this slice goes      */
#define FN_R_LOAD_N      0x40F    /* slices in this load / 4                 */
#define FN_R_LOAD_SEQ    0x410    /* bumps once per slice, 1..255            */
#define FN_R_LOAD_PCT    0x411    /* 0-100 across the whole load             */
#define FN_R_MODE        0x412    /* FN_MODE_*: what the cart is serving     */
#define FN_R_MAPPER      0x414    /* A78MAP_* of the live image              */
#define FN_R_LINK        0x415    /* 1 once the ESP32 link has been seen     */
#define FN_R_BOOT_GOT0   0x416    /* image bytes received so far, 24-bit LE  */
#define FN_R_BOOT_TOT0   0x419    /* image size declared at OPEN, 24-bit LE  */

/* 7800 additions. */
#define FN_R_INPTCTRL    0x41C    /* the cart's model of INPTCTRL            */
#define FN_R_INPT_LOCK   0x41D    /* 1 once a write locked it (power-cycle clears) */
#define FN_R_TV          0x41E    /* FN_TV_*: PAL if the BIOS ran from $C000, or FN_REG_TV */
#define FN_R_STAGED      0x41F    /* the staged image: FN_STAGED_* bits      */
#define FN_R_STAGED_KIND 0x420    /* its A78MAP_* kind                       */
#define FN_R_STAGED_CRC  0x421    /* its CRC-32, LE (4 bytes)                */
#define FN_R_HANDOVER    0x425    /* FN_HO_*: how the last image was started */
#define FN_R_HSC         0x426    /* FN_HSC_* bits                           */
#define FN_R_DIAG_RMW    0x427    /* RMW dummy writes dropped                */

#define FN_R_PAINT_END   0x4FC    /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* "FUJI" + 6 flag bytes at CPU $FF70, below the signature area, keeps the
 * mailbox live after the image boots. */
#define FN_CLAIM_ADDR    0xFF70
#define FN_CLAIM_LEN     10
#define FN_R_CLAIM_SIG   "FUJI"
#define FN_CLAIM_VER     4        /* flag byte 0: claim version (1)          */
#define FN_CLAIM_KIND    5        /* flag byte 1: A78MAP_* kind + 1, 0 = auto */
#define FN_CLAIM_FLAGS   6        /* flag byte 2: FN_CLAIMF_*                */
#define FN_CLAIMF_RAM    0x01     /* 16K cart RAM at $4000-$7FFF             */

#define FN_R_STATUS_LINK 0x01

/* ---------------- console -> cart: writes ---------------- */

/* Writing V to FN_H_REGSEL + n (n < 0x80) sets register n = V; the cart
 * synthesises the REGSEL/REGDATA pair fujimail.c decodes. */
#define FN_H_REGSEL      0x500    /* write V to +n: register n = V ($0D00)   */
#define FN_H_REGDATA     0x600    /* unused by clients; for the shared decoder */
#define FN_H_DATA        0x700    /* write anywhere in page: append data byte */

#define FN_H_PAGE_MASK   0xF00

/* Loader entry points. Jump, don't call: neither returns. */
#define FN_LOADER_BOOT   0x0600   /* load the armed image (or CONFIG) and start it */
#define FN_LOADER_CONFIG 0x0603   /* load CONFIG and start it                */

/* FN_H_REGSEL + 0x80..0xFF are one-shot operations, not registers. */
#define FN_HOT_SWAP      0xFE     /* LOAD: the armed image, else CONFIG      */
#define FN_HOT_GO        0xFD     /* start the loaded image now              */
#define FN_HOT_CONFIG    0xFC     /* LOAD CONFIG, whatever is armed          */
#define FN_HOT_GO_BIOS   0xFB     /* start it when the BIOS is mapped back in */

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
#define FN_REG_SLICE_ACK 0x14     /* loader: slice LOAD_SEQ has been copied  */
#define FN_REG_TV        0x15     /* FN_TV_*: the client's own measurement   */
#define FN_REG_HSC       0x16     /* FN_HSCOP_*                              */

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
#define FN_BOOT_READY    2        /* image staged; arm BOOTLOCK, then JMP $0600 */
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
#define FN_LOAD_SLICE    1        /* slice LOAD_SEQ is in the reply window: copy, ack */
#define FN_LOAD_DONE     2        /* all in place: the loader reads FN_R_HANDOVER and starts it */

/* FN_R_MODE values. */
#define FN_MODE_BOOT     0        /* power-on: the boot block, SRAM off      */
#define FN_MODE_LOAD     1        /* the loader is filling the SRAM          */
#define FN_MODE_GAME     2        /* an image in the SRAM, mailbox off       */
#define FN_MODE_APP      3        /* an image in the SRAM that claims the mailbox */

/* FN_R_TV values. */
#define FN_TV_NTSC       0
#define FN_TV_PAL        1

/* FN_R_STAGED bits. */
#define FN_STAGED_READY  0x01
#define FN_STAGED_CLAIM  0x02     /* a FujiNet app                           */
#define FN_STAGED_BIOSOK 0x04     /* this console's BIOS accepts it          */
#define FN_STAGED_HSCROM 0x08     /* it is the High Score Cart ROM           */

/* FN_R_HANDOVER values. */
#define FN_HO_NONE       0
#define FN_HO_BIOS       1        /* the console's own BIOS started it       */
#define FN_HO_DIRECT     2        /* the loader started it                   */

/* FN_R_HSC bits. */
#define FN_HSC_ROM       0x01     /* an HSC ROM is installed                 */
#define FN_HSC_ON        0x02     /* games get the HSC                       */
#define FN_HSC_SD        0x04     /* the last save reached the SD card       */
#define FN_HSC_DIRTY     0x08     /* unsaved high scores                     */

/* FN_REG_HSC operations. */
#define FN_HSCOP_OFF     0
#define FN_HSCOP_ON      1
#define FN_HSCOP_FORGET  2        /* drop the installed ROM                  */
#define FN_HSCOP_INSTALL 3        /* the staged image (FN_STAGED_HSCROM) becomes
                                     the HSC ROM, and the HSC goes on        */

/* Booting: poll FN_R_BOOT_STATE for READY, write FN_REG_BOOTLOCK =
 * FN_BOOTLOCK_MAGIC, then SEI and JMP FN_LOADER_BOOT. The client's next SEQ
 * is always FN_R_ACKSEQ + 1 (255 wraps to 1), never a local counter. */

#endif /* FUJI_MAILBOX_H */
