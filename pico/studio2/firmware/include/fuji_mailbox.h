/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the RCA Studio II.
 * Shared by the RP2354B firmware, the MAME device and the host tests;
 * testrom/fujinet.inc mirrors it by hand (tools/checkdefs.py compares them).
 *
 * The Studio II edge has no write strobe, so every console -> cart byte is
 * carried in the ADDRESS of a read, as on the 5200. Unlike the 5200 the cart
 * sees the full 16-bit address of every read (MA0-7 plus TPA), so the arena
 * sits high, clear of the BIOS and of console RAM:
 *
 *   $E400-$E7FF  1K reply window cart-painted, read in place
 *   $E800-$E8FF  status page     cart-painted
 *   $E900-$E9FF  REGSEL          a read of $E900+n selects register n;
 *                                $E980-$E9FF are not registers (the stub)
 *   $EA00-$EAFF  REGDATA         a read of $EA00+v writes v to it
 *   $EB00-$EBFF  TX stream       a read of $EB00+b appends byte b
 *   $EC00-$EFFF  text engine     s2_text.h
 *   $F400-$F7FF  raster          64x128, the 1861's DMA source
 *   $F800-$FFFF  raster mirror   served by position, no side effects
 *
 * Touch the hotspot pages only with LDN through the RF macros (fujilib.inc).
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* ---------------- address map ---------------- */

#define FN_ARENA_BASE    0xE400   /* CPU address of the arena                */
#define FN_ARENA_SIZE    0x0800   /* reply, status, REGSEL, REGDATA, TX      */

#define FN_TEXT_BASE     0xEC00   /* s2_text.h hotspot pages                 */
#define FN_TEXT_SIZE     0x0400
#define FN_RASTER_BASE   0xF400
#define FN_RASTER_SIZE   0x0400
#define FN_RASTER_END    0x10000  /* raster plus its mirror                  */

/* ---------------- cart -> console: painted memory ---------------- */

#define FN_R_DATA        0x000    /* the 1K reply ($E400)                    */
#define FN_R_SLICE_LEN   0x400
#define FN_R_NSLICES     1        /* x 1024 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x400    /* first painted status byte ($E800)       */
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
#define FN_R_LINK        0x415    /* 1 once the ESP32 link has been seen     */
#define FN_R_BOOT_GOT0   0x416    /* image bytes received so far, 24-bit LE  */
#define FN_R_BOOT_TOT0   0x419    /* image size declared at OPEN, 24-bit LE  */
#define FN_R_ARMED       0x41C    /* 1 once the swap is armed                */
#define FN_R_STAGED      0x41D    /* the staged image: FN_STAGED_* bits      */
#define FN_R_STAGED_CRC  0x41F    /* its CRC-32, LE (4 bytes)                */
#define FN_R_SWAPS       0x423    /* hand-overs taken since power-on, mod 256 */
#define FN_R_FRAMES      0x424    /* raster frames served, mod 256           */

#define FN_R_PAINT_END   0x4FC    /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* "FUJI" at $07FC, the last bytes of the window every image has, keeps the
 * mailbox live after the image boots. */
#define FN_CLAIM_ADDR    0x07FC
#define FN_R_CLAIM_SIG   "FUJI"

#define FN_R_STATUS_LINK 0x01

/* ---------------- console -> cart: reads ---------------- */

#define FN_H_REGSEL      0x500    /* read +n (n < 0x80): select register n   */
#define FN_H_REGDATA     0x600    /* read +v: selected register = v          */
#define FN_H_DATA        0x700    /* read +b: append data byte b             */

#define FN_H_PAGE_MASK   0xF00

/* FN_H_REGSEL + 0x80..0xFF are not registers. The cart serves the hand-over's
 * last three bytes at FN_STUB -- SEX RC; RET; $23 -- run with P=C. RET reads
 * the $23 (X=2, P=3, IE=1) at FN_STUB_T and the CPU's next read is the BIOS
 * reset code at $0000 (R3 = 0): the cart swaps on that read, and only when
 * the read before it was FN_STUB_T. */
#define FN_HOT_STUB      0xF0
#define FN_HOT_T         0xF2
#define FN_STUB          (FN_ARENA_BASE + FN_H_REGSEL + FN_HOT_STUB)
#define FN_STUB_T        (FN_ARENA_BASE + FN_H_REGSEL + FN_HOT_T)

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

/* DBC push streams (lib/device/rs232/disk.cpp). */
#define FN_STREAM_ROM    0
#define FN_STREAM_CFG    1

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2        /* staged; BOOTLOCK, wait for ARMED, hand over */
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
 * FN_BOOTLOCK_MAGIC, poll FN_R_ARMED, then fn_handover (testrom/s2call.inc).
 * The client's next SEQ is always FN_R_ACKSEQ + 1 (255 wraps to 1), never a
 * local counter. */

#endif /* FUJI_MAILBOX_H */
