/* fuji_mailbox_proto.h -- the mailbox protocol every cartridge shares: register numbers,
 * magics, status values and limits. Each cart's fuji_mailbox.h includes this and adds its
 * own address map and platform registers; clients mirror both by hand.
 *
 * Registers 0x00-0x13 belong to the protocol; 0x14 and up are each platform's.
 */

#ifndef FUJI_MAILBOX_PROTO_H
#define FUJI_MAILBOX_PROTO_H

/* FN_R_STATUS bits. */
#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* An image carrying this where its platform says claims the mailbox. */
#define FN_R_CLAIM_SIG   "FUJI"
#define FN_R_CLAIM_LEN   4

/* Registers, reached through a REGSEL/REGDATA pair. */
#define FN_REG_DEVICE    0x00     /* FujiBus device id, e.g. 0x70            */
#define FN_REG_CMD       0x01     /* FujiBus command id                      */
#define FN_REG_NPARAM    0x02     /* number of parameters in the TX stream   */
#define FN_REG_DATA_RST  0x05     /* any value: rewind the TX write pointer  */
#define FN_REG_RXSLICE   0x06     /* which reply slice FN_R_DATA shows       */
#define FN_REG_SEQ       0x10     /* nonzero, != ACKSEQ: launch transaction  */
#define FN_REG_BOOTLOCK  0x11     /* FN_BOOTLOCK_MAGIC: arm the staged image */
#define FN_REG_BOOTSEL_1 0x12     /* FN_BOOTSEL_MAGIC1, then...              */
#define FN_REG_BOOTSEL_2 0x13     /* ...FN_BOOTSEL_MAGIC2: reboot to BOOTSEL */

#define FN_BOOTLOCK_MAGIC 0xB5
/* BOOTSEL takes two register writes, so no single stray access can trigger it. */
#define FN_BOOTSEL_MAGIC1 0xB5
#define FN_BOOTSEL_MAGIC2 0x4A

/* TX stream: NPARAM x {size 1|2|4, value LE}, then the payload. */
#define FN_TX_MAX        320

/* DBC push streams: the image, and an optional .cfg sibling whose `mapper=` line
 * overrides the cart's mapper choice on carts that have one. */
#define FN_STREAM_ROM    0
#define FN_STREAM_CFG    1

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2
#define FN_BOOT_FAILED   0x80

/* FN_R_BOOT_ERR values. */
#define FN_BOOT_ERR_TOOBIG    1
#define FN_BOOT_ERR_TRUNCATED 2
#define FN_BOOT_ERR_NOMAP     3
#define FN_BOOT_ERR_STOREBUSY 4   /* the only store that fits is being served from */

/* FN_R_ERR values; mirrors fb_status_t in fujibus.h. */
#define FN_ERR_OK        0
#define FN_ERR_NOLINK    1
#define FN_ERR_TIMEOUT   2
#define FN_ERR_BADFRAME  3
#define FN_ERR_TOOBIG    4

#endif /* FUJI_MAILBOX_PROTO_H */
