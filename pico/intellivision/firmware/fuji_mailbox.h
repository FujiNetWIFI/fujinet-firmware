#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

// FujiNet mailbox: Intellivision-visible RAM window $9800-$9FFF, i.e.
// RAM[0x1800..0x1FFF] (RAM[] is indexed by addr-0x8000; the whole $8000-
// $9FFF window is RAM in the boot map set up in Inty_cart_main()). Every
// cell holds one byte in the low 8 bits -- writes from the Intellivision
// side are hardware-truncated to 8 bits regardless (see the DWS handler in
// core1_main()), so keeping the RP2040->Inty direction byte-wide too keeps
// jzIntv (narrow RAM) and real hardware consistent.
//
// Handshake: Inty fills in DEVICE/CMD/NPARAM/params/TX payload, then bumps
// SEQ last. The RP2040 notices SEQ != ACKSEQ, runs the transaction, fills
// in the reply, and publishes it by writing ACKSEQ = SEQ last. This is a
// sequence-number interlock, not a status flag: re-poking SEQ with the same
// value while a transaction is in flight is a no-op, so a timed-out Inty
// retry can never cause the RP2040 to replay a command.
//
// This header is the single source of truth for the RP2040 side. The
// IntyBASIC side (intv/fujitest.bas) hardcodes the same offsets as CONSTs
// -- IntyBASIC has no #include mechanism for sharing this file, so keep the
// two in sync by hand if this layout ever changes.

#define FUJI_MB_BASE       0x1800 // RAM[] index of Intellivision address $9800

#define FUJI_MB_MAGIC0     (FUJI_MB_BASE + 0x00) // RP2040 writes 'F'
#define FUJI_MB_MAGIC1     (FUJI_MB_BASE + 0x01) // RP2040 writes 'N'
#define FUJI_MB_PROTO_VER  (FUJI_MB_BASE + 0x02) // RP2040 writes 1
#define FUJI_MB_SEQ        (FUJI_MB_BASE + 0x03) // Inty: bump (wrapping, skip 0) to start a transaction
#define FUJI_MB_ACKSEQ     (FUJI_MB_BASE + 0x04) // RP2040: == SEQ once the reply is ready
#define FUJI_MB_DEVICE     (FUJI_MB_BASE + 0x05) // Inty: request device id
#define FUJI_MB_CMD        (FUJI_MB_BASE + 0x06) // Inty: request command id
#define FUJI_MB_NPARAM     (FUJI_MB_BASE + 0x07) // Inty: param count (0-8)
#define FUJI_MB_TXLEN_LO   (FUJI_MB_BASE + 0x08) // Inty: payload length, LE
#define FUJI_MB_TXLEN_HI   (FUJI_MB_BASE + 0x09)
#define FUJI_MB_STATUS     (FUJI_MB_BASE + 0x0A) // RP2040: diagnostic only -- see FUJI_MB_STATUS_*, poll ACKSEQ not this
#define FUJI_MB_ERR        (FUJI_MB_BASE + 0x0B) // RP2040: fb_status_t of the last transaction
#define FUJI_MB_RXLEN_LO   (FUJI_MB_BASE + 0x0C) // RP2040: reply payload length, LE
#define FUJI_MB_RXLEN_HI   (FUJI_MB_BASE + 0x0D)
#define FUJI_MB_REPLY_CMD  (FUJI_MB_BASE + 0x0E) // RP2040: FUJICMD_ACK (0x06) or FUJICMD_NAK (0x15)
#define FUJI_MB_LINK       (FUJI_MB_BASE + 0x0F) // RP2040: 1 if the ESP32-S3 is enumerated (tud_cdc_connected())

#define FUJI_MB_PARAM_SIZE (FUJI_MB_BASE + 0x10) // Inty: 8 entries, 1 word each -- size in bytes (1/2/4)
#define FUJI_MB_PARAM_VAL  (FUJI_MB_BASE + 0x20) // Inty: 8 entries x 4 words, little-endian

#define FUJI_MB_TX         (FUJI_MB_BASE + 0x40)  // Inty: request payload
#define FUJI_MB_TX_MAX     256

#define FUJI_MB_RX         (FUJI_MB_BASE + 0x140) // RP2040: reply payload
#define FUJI_MB_RX_MAX      1536

#define FUJI_MB_STATUS_IDLE  0
#define FUJI_MB_STATUS_BUSY  1
#define FUJI_MB_STATUS_OK    2
#define FUJI_MB_STATUS_ERR   3

#endif /* FUJI_MAILBOX_H */
