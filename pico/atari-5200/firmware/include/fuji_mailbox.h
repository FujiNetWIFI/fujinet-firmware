/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Atari 5200.
 * Shared by the RP2354B firmware, the MAME device and the host tests;
 * testrom/fujinet.inc and fujinet-lib's atari5200 target mirror it by hand
 * (tools/checkdefs.py compares them).
 *
 * The 5200 edge has no R/W line, so the cart never sees CPU data: every
 * console -> cart byte is carried in the ADDRESS of a read, as on the
 * ColecoVision. The arena keeps the family's offsets, placed in the top of
 * the cart window where a FujiNet app leaves room for it:
 *
 *   $B000-$B3FF  1K reply window cart-painted, read in place
 *   $B400-$B4FF  status page     cart-painted
 *   $B500-$B5FF  REGSEL          a read of $B500+n selects register n;
 *                                $B580-$B5FF are operations and the stub
 *   $B600-$B6FF  REGDATA         a read of $B600+v writes v to it
 *   $B700-$B7FF  TX stream       a read of $B700+b appends byte b
 *
 * Read the hotspot pages only with LDA/LDX/LDY/BIT abs or abs,X/Y from a
 * page-aligned base, never twice in a row at one address, never with an RMW
 * instruction or (zp),Y, and never point ANTIC at the arena.
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* ---------------- address map ---------------- */

#define FN_WINDOW_BASE   0x4000   /* the cart window, $4000-$BFFF            */
#define FN_WINDOW_SIZE   0x8000
#define FN_ARENA_BASE    0xB000   /* CPU address of the arena                */
#define FN_ARENA_SIZE    0x0800   /* 2K                                      */
#define FN_ARENA_OFF     (FN_ARENA_BASE - FN_WINDOW_BASE)  /* window offset  */

/* ---------------- cart -> console: painted memory ---------------- */

#define FN_R_DATA        0x000    /* the 1K reply ($B000)                    */
#define FN_R_SLICE_LEN   0x400
#define FN_R_NSLICES     1        /* x 1024 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x400    /* first painted status byte ($B400)       */
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
#define FN_R_MODE        0x412    /* FN_MODE_*: what the cart is serving     */
#define FN_R_MAPPER      0x414    /* A52MAP_* of the live image              */
#define FN_R_LINK        0x415    /* 1 once the ESP32 link has been seen     */
#define FN_R_BOOT_GOT0   0x416    /* image bytes received so far, 24-bit LE  */
#define FN_R_BOOT_TOT0   0x419    /* image size declared at OPEN, 24-bit LE  */

/* 5200 additions. */
#define FN_R_ARMED       0x41C    /* 1 once the swap is armed: then JMP FN_STUB */
#define FN_R_STAGED      0x41D    /* the staged image: FN_STAGED_* bits      */
#define FN_R_STAGED_KIND 0x41E    /* its A52MAP_* kind                       */
#define FN_R_STAGED_CRC  0x41F    /* its CRC-32, LE (4 bytes)                */
#define FN_R_SWAPS       0x423    /* hand-overs taken since power-on, mod 256 */

#define FN_R_PAINT_END   0x4FC    /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* "FUJI" + 3 flag bytes at CPU $BFE0, just under the BIOS's header, keeps
 * the mailbox live after the image boots. $BFE7 is not part of it: the 2-port
 * BIOS reads it on a PAL GTIA, so apps leave $02 ("PAL-compatible") there. */
#define FN_CLAIM_ADDR    0xBFE0
#define FN_CLAIM_LEN     7
#define FN_R_CLAIM_SIG   "FUJI"
#define FN_CLAIM_VER     4        /* flag byte 0: claim version (1)          */
#define FN_CLAIM_KIND    5        /* flag byte 1: A52MAP_* kind + 1, 0 = auto */
#define FN_CLAIM_FLAGS   6        /* flag byte 2: reserved, 0                */
#define FN_PALCOMPAT_ADDR 0xBFE7
#define FN_PALCOMPAT     0x02

#define FN_R_STATUS_LINK 0x01

/* ---------------- console -> cart: reads ---------------- */

#define FN_H_REGSEL      0x500    /* read +n (n < 0x80): select register n   */
#define FN_H_REGDATA     0x600    /* read +v: selected register = v          */
#define FN_H_DATA        0x700    /* read +b: append data byte b             */

#define FN_H_PAGE_MASK   0xF00

/* FN_H_REGSEL + 0x80..0xFF are not registers. The cart serves a three-byte
 * JMP ($FFFC) at FN_STUB; the read of its last byte swaps in the armed image,
 * which is the last cart read before the CPU fetches the BIOS's reset code.
 * Unarmed, the CPU just reboots what is already there. */
#define FN_HOT_STUB      0xF0     /* $B5F0: 6C FC FF                         */
#define FN_HOT_SWAP      0xF2     /* the read that swaps                     */
#define FN_STUB          (FN_ARENA_BASE + FN_H_REGSEL + FN_HOT_STUB)

/* Registers; the numbering matches every sibling. */
#define FN_REG_DEVICE    0x00     /* FujiBus device id, e.g. 0x70            */
#define FN_REG_CMD       0x01     /* FujiBus command id                      */
#define FN_REG_NPARAM    0x02     /* number of parameters in the TX stream   */
#define FN_REG_DATA_RST  0x05     /* any value: rewind the TX write pointer  */
#define FN_REG_RXSLICE   0x06     /* which reply slice FN_R_DATA shows       */
#define FN_REG_SEQ       0x10     /* nonzero, != ACKSEQ: launch transaction  */
#define FN_REG_BOOTLOCK  0x11     /* FN_BOOTLOCK_MAGIC: arm the staged image */
#define FN_REG_BOOTSEL_1 0x12     /* FN_BOOTSEL_MAGIC1, then...              */
#define FN_REG_BOOTSEL_2 0x13     /* ...FN_BOOTSEL_MAGIC2: reboot to BOOTSEL */
#define FN_REG_CONFIG    0x14     /* FN_CONFIG_MAGIC: stage and arm CONFIG   */

#define FN_BOOTLOCK_MAGIC 0xB5
#define FN_BOOTSEL_MAGIC1 0xB5
#define FN_BOOTSEL_MAGIC2 0x4A
#define FN_CONFIG_MAGIC   0xC5

/* TX stream: NPARAM x {size 1|2|4, value LE}, then the payload. */
#define FN_TX_MAX        320

/* DBC push streams (lib/device/rs232/disk.cpp); a .cfg `mapper=` line
 * overrides the cart's mapper choice. */
#define FN_STREAM_ROM    0
#define FN_STREAM_CFG    1

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2        /* staged; BOOTLOCK, wait for ARMED, JMP FN_STUB */
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

/* FN_R_MODE values. */
#define FN_MODE_BOOT     0        /* power-on: the baked CONFIG              */
#define FN_MODE_GAME     2        /* an image, mailbox off                   */
#define FN_MODE_APP      3        /* an image that claims the mailbox        */

/* FN_R_STAGED bits. */
#define FN_STAGED_READY  0x01
#define FN_STAGED_CLAIM  0x02     /* a FujiNet app                           */
#define FN_STAGED_CONFIG 0x04     /* it is the baked CONFIG                  */

/* Booting: poll FN_R_BOOT_STATE for READY, write FN_REG_BOOTLOCK =
 * FN_BOOTLOCK_MAGIC, poll FN_R_ARMED, quiet the machine, then JMP FN_STUB.
 * The client's next SEQ is always FN_R_ACKSEQ + 1 (255 wraps to 1), never a
 * local counter. */

#endif /* FUJI_MAILBOX_H */
