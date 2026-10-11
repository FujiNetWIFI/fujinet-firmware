/* fujilib.h -- the console's half of the FujiNet mailbox, in C.
 *
 * Mirrors firmware/include/fuji_mailbox.h by hand; keep the two in step.
 * Console -> cart is a store to a hotspot page; cart -> console is memory the
 * cart paints, read in place.
 */

#ifndef FUJILIB_H
#define FUJILIB_H

#include <stdbool.h>

/* ---- cart -> console ---- */
#define FN_REPLY     ((volatile unsigned char *)0xB000)     /* 1024 bytes */
#define FN_REPLY_MAX 1024

#define FN_ACKSEQ    (*(volatile unsigned char *)0xB400)
#define FN_STATUS    (*(volatile unsigned char *)0xB401)
#define FN_ERRCODE   (*(volatile unsigned char *)0xB402)
#define FN_REPLYCMD  (*(volatile unsigned char *)0xB403)
#define FN_RXLEN_LO  (*(volatile unsigned char *)0xB404)
#define FN_RXLEN_HI  (*(volatile unsigned char *)0xB405)
#define FN_BOOTSTAT  (*(volatile unsigned char *)0xB406)
#define FN_BOOTPCT   (*(volatile unsigned char *)0xB407)
#define FN_BOOTERR   (*(volatile unsigned char *)0xB408)
#define FN_MAGIC0    (*(volatile unsigned char *)0xB409)
#define FN_MAGIC1    (*(volatile unsigned char *)0xB40A)
#define FN_PROTOVER  (*(volatile unsigned char *)0xB40B)
#define FN_MODE      (*(volatile unsigned char *)0xB412)
#define FN_LINK      (*(volatile unsigned char *)0xB415)

/* ---- console -> cart: one store per register ---- */
#define FN_REGSEL    0xB500
#define FN_TXPAGE    (*(volatile unsigned char *)0xB700)

#define FNR_DEVICE   0x00
#define FNR_CMD      0x01
#define FNR_NPARAM   0x02
#define FNR_DATA_RST 0x05
#define FNR_RXSLICE  0x06
#define FNR_SEQ      0x10
#define FNR_BOOTLOCK 0x11

#define FN_BOOTLOCK_MAGIC 0xB5
#define FN_LOADER_BOOT    0xB800
#define FN_LOADER_CONFIG  0xB803

/* FujiBus */
#define FN_DEV_FUJINET 0x70
#define FN_ACK         0x06
#define FN_NAK         0x15

/* fn_commit() results: FN_ERR_* from fuji_mailbox.h, plus our own timeout. */
#define FN_OK        0
#define FN_ENOLINK   1
#define FN_ETIMEOUT  2
#define FN_EBADFRAME 3
#define FN_ETOOBIG   4
#define FN_EWAIT     0xFF

/* Boot states */
#define FNB_IDLE   0
#define FNB_XFER   1
#define FNB_READY  2
#define FNB_FAILED 0x80

/* Is a FujiNet cartridge underneath us? */
bool fn_present(void);

/* Begin a transaction: rewind the TX stream and address a device. */
void fn_start(unsigned char device, unsigned char command);

/* Parameters, in order, before any payload. */
void fn_param8(unsigned char v);
void fn_param16(unsigned int v);

/* Payload. fn_tx_padded pads with NULs to `total` (SET_DEVICE_FULLPATH and
 * OPEN_DIRECTORY take a fixed 256-byte buffer). */
void fn_tx(unsigned char b);
void fn_tx_bytes(const unsigned char *p, unsigned int n);
void fn_tx_padded(const char *s, unsigned int total);

/* Stream a reply-window string into the TX page behind `prefix`, padded. */
void fn_tx_path(const char *prefix, volatile unsigned char *name, unsigned int total);

/* Launch and wait. The sequence number is always the cart's FN_ACKSEQ + 1,
 * never a variable of ours: a console Reset restarts this program but not
 * the cart. Returns FN_OK / FN_E*. */
unsigned char fn_commit(void);

bool fn_acked(void);
unsigned int fn_reply_len(void);

/* Arm the staged image and jump into the cart's loader. Does not return. */
void fn_boot(void);

/* Hand the console back to CONFIG. Does not return. */
void fn_exit_to_config(void);

#endif /* FUJILIB_H */
