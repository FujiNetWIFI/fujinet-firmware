/* fujilib.h -- the console's half of the FujiNet mailbox, in C.
 *
 * Mirrors firmware/include/fuji_mailbox.h by hand; keep the two in step. Every
 * address here is a CONSOLE address (image offset + $8000).
 *
 * Both directions of the mailbox are cartridge READS, because the ColecoVision
 * cartridge port has no write strobe -- see fuji_mailbox.h for why. So:
 *
 *   console -> cart   the ADDRESS carries the payload. Reading $FD05 arms
 *                     register 5; reading $FE2A then sets it to 0x2A. Reading
 *                     $FF41 appends 'A' to the outgoing stream.
 *   cart -> console   the cartridge repaints its own ROM, so the reply is
 *                     just bytes at $F800 by the time we look.
 *
 * The reply window is the whole 1K in one piece rather than a paged 256-byte
 * slice as on the Astrocade and the Arcadia. That is the one place this port
 * spends its bigger window on the console's behalf: the ColecoVision has about
 * 700 usable bytes of RAM after OS7's tables and the stack, so a directory
 * entry that can be read in place, out of cartridge ROM, is worth more than
 * the 1K of client space it costs.
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
void fn_param8(unsigned char v);
void fn_param16(unsigned int v);

/* Payload. fn_tx_padded streams a NUL-terminated string and then pads with
 * NULs to `total` bytes -- SET_DEVICE_FULLPATH and OPEN_DIRECTORY take a
 * fixed 256-byte buffer and a short one is rejected on the ESP32 side. */
void fn_tx(unsigned char b);
void fn_tx_bytes(const unsigned char *p, unsigned int n);
void fn_tx_padded(const char *s, unsigned int total);

/* Launch, and wait for the reply. Returns FN_OK / FN_E*.
 *
 * The sequence number comes from the cartridge's own FN_ACKSEQ + 1, never from
 * a variable of ours. A console RESET restarts this program and re-zeroes
 * everything it owns, but does NOT reset the cartridge -- deriving the
 * sequence locally means every reset replays the same number, the cart sees a
 * sequence it has already acknowledged, and no request is ever sent again
 * while the stale reply sits there looking like success. That bug cost the
 * Intellivision bring-up a whole session; it is designed out here. */
unsigned char fn_commit(void);

/* Did the far side ACK? Only meaningful after fn_commit() == FN_OK. */
bool fn_acked(void);

/* Length of the reply currently in FN_REPLY. */
unsigned int fn_reply_len(void);

/* Arm and take the ROM swap. Does not return. */
void fn_boot_swap(void);

#endif /* FUJILIB_H */

/* --- streaming out of the reply window ---
 *
 * SET_DEVICE_FULLPATH and OPEN_DIRECTORY take a fixed 256-byte buffer, and the
 * name to put in it is already sitting in the reply window as cartridge ROM.
 * The reply is only repainted by a SEQ commit or an RXSLICE select (see the
 * invariant in fuji_mailbox.h), so it survives while the next transaction's TX
 * stream is being built -- and a directory entry can go from the cartridge to
 * the cartridge without ever occupying any of the console's ~700 bytes of RAM.
 */
void fn_tx_path(const char *prefix, volatile unsigned char *name,
                unsigned int total);

/* Stream `n` bytes of the reply window, starting at `off`, into the outgoing
 * payload. This is what lets WRITE_HOST_SLOTS -- which takes all eight 32-byte
 * slots in one 256-byte payload, with no per-slot form -- be answered without a
 * 256-byte mirror in a machine that has under a kilobyte of RAM: read the eight
 * slots, then stream them straight back with the edited one substituted. */
void fn_tx_from_reply(unsigned int off, unsigned int n);
