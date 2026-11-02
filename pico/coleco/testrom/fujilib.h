/* fujilib.h -- the console's half of the FujiNet mailbox, in C. Mirrors
 * firmware/include/fuji_mailbox.h by hand, as CONSOLE addresses (image offset + $8000).
 *
 * Both directions are cartridge reads: reading $FD05 arms register 5 and $FE2A then sets it
 * to 0x2A; reading $FF41 appends 'A' to the TX stream. The reply is bytes the cart painted
 * at $F800, the whole 1K in one piece, so the client reads it in place: the console has
 * ~700 bytes of RAM to spare.
 */

#ifndef FUJILIB_H
#define FUJILIB_H

#include <stdbool.h>

/* ---- cart -> console: repainted ROM ---- */
#define FN_REPLY    ((volatile unsigned char *)0xF800)  /* 1024 bytes */
#define FN_REPLY_MAX 1024

#define FN_ACKSEQ   (*(volatile unsigned char *)0xFC00)
#define FN_STATUS   (*(volatile unsigned char *)0xFC01)
#define FN_ERRCODE  (*(volatile unsigned char *)0xFC02)
#define FN_REPLYCMD (*(volatile unsigned char *)0xFC03)
#define FN_RXLEN_LO (*(volatile unsigned char *)0xFC04)
#define FN_RXLEN_HI (*(volatile unsigned char *)0xFC05)
#define FN_BOOTSTAT (*(volatile unsigned char *)0xFC06)
#define FN_BOOTPCT  (*(volatile unsigned char *)0xFC07)
#define FN_BOOTERR  (*(volatile unsigned char *)0xFC08)
#define FN_MAGIC0   (*(volatile unsigned char *)0xFC09)
#define FN_MAGIC1   (*(volatile unsigned char *)0xFC0A)
#define FN_PROTOVER (*(volatile unsigned char *)0xFC0B)

/* ---- console -> cart: hotspot reads ---- */
#define FN_REGSEL   ((volatile unsigned char *)0xFD00)
#define FN_REGDAT   ((volatile unsigned char *)0xFE00)
#define FN_TXPAGE   ((volatile unsigned char *)0xFF00)
#define FN_SWAP     (*(volatile unsigned char *)0xFDFE)

/* Register file */
#define FNR_DEVICE   0x00
#define FNR_CMD      0x01
#define FNR_NPARAM   0x02
#define FNR_DATA_RST 0x05
#define FNR_RXSLICE  0x06
#define FNR_SEQ      0x10
#define FNR_BOOTLOCK 0x11

#define FN_BOOTLOCK_MAGIC 0xB5

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
#define FN_EWAIT     0xFF   /* the cart never answered at all */

/* Boot states */
#define FNB_IDLE   0
#define FNB_XFER   1
#define FNB_READY  2
#define FNB_FAILED 0x80

/* Is a FujiNet cartridge actually underneath us? On a plain ROM cart these
 * two addresses are whatever the game put there (or 0xFF). */
bool fn_present(void);

/* Begin a transaction: rewind the outgoing stream and address a device. */
void fn_start(unsigned char device, unsigned char command);

/* Parameters, in order, before any payload. */
void fn_param8(unsigned char value);
void fn_param16(unsigned int value);

/* Payload. fn_tx_padded streams a NUL-terminated string and then pads with
 * NULs to `total` bytes -- SET_DEVICE_FULLPATH and OPEN_DIRECTORY take a
 * fixed 256-byte buffer and a short one is rejected on the ESP32 side. */
void fn_tx(unsigned char byte);
void fn_tx_bytes(const unsigned char *src, unsigned int count);
void fn_tx_padded(const char *str, unsigned int total);

/* Launch, and wait for the reply. Returns FN_OK / FN_E*. The sequence number is the cart's
 * FN_ACKSEQ + 1, never a variable of ours: a console RESET doesn't reset the cart, and a
 * replayed number is never answered. */
unsigned char fn_commit(void);

/* Did the far side ACK? Only meaningful after fn_commit() == FN_OK. */
bool fn_acked(void);

/* Length of the reply currently in FN_REPLY. */
unsigned int fn_reply_len(void);

/* Arm and take the ROM swap. Does not return. */
void fn_boot_swap(void);

/* Streaming out of the reply window, which only a SEQ commit repaints: a name for
 * SET_DEVICE_FULLPATH or OPEN_DIRECTORY's 256-byte buffer goes from the reply straight into
 * the TX stream, padded to `total`, without passing through console RAM. */
void fn_tx_path(const char *prefix, volatile unsigned char *name,
                unsigned int total);

/* Stream `count` reply bytes from `off` into the payload; WRITE_HOST_SLOTS sends the eight
 * slots it read back this way, with only the edited one from RAM. */
void fn_tx_from_reply(unsigned int off, unsigned int count);

#endif /* FUJILIB_H */
