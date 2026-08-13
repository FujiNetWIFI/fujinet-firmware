#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

// FujiNet mailbox: Intellivision-visible RAM window $9C00-$9F3F, i.e.
// RAM[0x1C00..0x1F3F] (RAM[] is indexed by addr-0x8000). Every cell holds
// one byte in the low 8 bits -- writes from the Intellivision side are
// hardware-truncated to 8 bits regardless (see the DWS handler in
// core1_main()), so keeping the RP2040->Inty direction byte-wide too keeps
// jzIntv (narrow RAM) and real hardware consistent.
//
// Unlike the original PiRTO II port, this window does NOT claim all of
// $8000-$9FFF: on a Minty cartridge $8000-$9FFF is JLP RAM, and JLP games
// need almost all of it. The mailbox is deliberately placed at the *top*
// of the window so $8000-$9BFF stays a contiguous free block for JLP RAM
// (7168 of 8192 words, ~87%). See cartridge.c for how the JLP window and
// this mailbox window share the same RAM-claim branch in the bus loop.
//
// Handshake: Inty fills in DEVICE/CMD/NPARAM/params/TX payload, then bumps
// SEQ last. The RP2040 notices SEQ != ACKSEQ, runs the transaction, fills
// in the reply, and publishes it by writing ACKSEQ = SEQ last. This is a
// sequence-number interlock, not a status flag: re-poking SEQ with the same
// value while a transaction is in flight is a no-op, so a timed-out Inty
// retry can never cause the RP2040 to replay a command.
//
// This header is the single source of truth for the RP2040 side. The
// IntyBASIC side (fujinet-config/intv/fujinet.bas) hardcodes the same
// offsets as CONSTs -- IntyBASIC has no #include mechanism for sharing this
// file, so keep the two in sync by hand if this layout ever changes.

#define FUJI_MB_BASE       0x1C00 // RAM[] index of Intellivision address $9C00

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

// Boot progress, published out-of-band from the ACKSEQ handshake: a
// MOUNT_IMAGE transaction for a ROM+.cfg pair can run far longer than an
// ordinary mailbox round trip (the ESP32 pushes the file(s) to us over the
// same USB-CDC link that mailbox transactions use, mid-transaction -- see
// the "ROM boot receiver" section and dbc_inbound_handler() in
// fujinet.c). The Inty's own transact loop can PEEK these cells while
// it waits on ACKSEQ, so it can draw a progress bar instead of just
// sitting on a blank "BOOTING" screen. They live in the gap between
// PARAM_SIZE (8 words, ends at +0x18) and PARAM_VAL (starts at +0x20)
// that was otherwise unused.
#define FUJI_MB_BOOT_STATE (FUJI_MB_BASE + 0x18) // RP2040: FUJI_BOOT_* below
#define FUJI_MB_BOOT_PCT   (FUJI_MB_BASE + 0x19) // RP2040: 0-100
#define FUJI_MB_BOOT_ERR   (FUJI_MB_BASE + 0x1A) // RP2040: reason FUJI_BOOT_FAILED, if it is

// BOOTSEL doorbell: on the single-USB-port board (no external RP2040 flash
// port), this is how the console side asks the RP2040 to reboot into
// BOOTSEL/PICOBOOT mode over the internal USB link to the ESP32-S3, which
// then reflashes it. Independent of the SEQ/ACKSEQ transaction interlock
// above -- write the exact magic byte to ring the doorbell; anything else
// is ignored, so a stray write elsewhere in the mailbox can't accidentally
// reboot the cart mid-game. Serviced by fuji_mailbox_service(); the RP2040
// does not return from this, so there is no ack to poll for.
#define FUJI_MB_BOOTSEL_DOORBELL  (FUJI_MB_BASE + 0x1B) // Inty: write FUJI_MB_BOOTSEL_MAGIC to request BOOTSEL
#define FUJI_MB_BOOTSEL_MAGIC     0xB5

#define FUJI_BOOT_IDLE     0
#define FUJI_BOOT_OPENING  1
#define FUJI_BOOT_XFER     2
#define FUJI_BOOT_MAPPING  3
#define FUJI_BOOT_FAILED   0x80

// FUJI_MB_BOOT_ERR reason codes, valid only when BOOT_STATE == FUJI_BOOT_FAILED.
#define FUJI_BOOT_ERR_REJECTED  1 // header magic present but malformed (bad addr range)
#define FUJI_BOOT_ERR_TRUNCATED 2 // Intellicart-header stream ended mid-segment
#define FUJI_BOOT_ERR_NOMAP     3 // no .cfg/header, and byte count matches no known size
#define FUJI_BOOT_ERR_MAILBOX   4 // a segment would overlap the $9C00-$9F3F mailbox window
#define FUJI_BOOT_ERR_CFGBAD    5 // a .cfg stream arrived but had no parseable mapping line

#define FUJI_MB_TX         (FUJI_MB_BASE + 0x40)  // Inty: request payload
#define FUJI_MB_TX_MAX     256                    // fujicmd.bas sends fixed 256-byte
                                                   // NUL-padded payloads for several
                                                   // commands (matches the ESP32's
                                                   // fixed MAX_FILENAME_LEN=256
                                                   // transaction_get() buffer) --
                                                   // do not shrink this one.

#define FUJI_MB_RX         (FUJI_MB_BASE + 0x140) // RP2040: reply payload
#define FUJI_MB_RX_MAX      512                   // largest replies in use today: the
                                                   // 256-byte host-slot block and the
                                                   // 240-byte AdapterConfigExtended

// fujibus_transact()'s raw USB receive buffer (fuji_mailbox_service()'s
// rxbuf) has to be sized separately from FUJI_MB_RX_MAX above: that constant
// bounds the mailbox's cart.RAM-resident reply window (a real address-space
// budget), but the same buffer also has to hold every complete SLIP-encoded
// frame the ESP32 pushes to FUJI_DEVICEID_DBC mid-MOUNT_IMAGE --
// MediaTypeROM::push_stream() (rs232/diskTypeROM.cpp) streams ROM/.cfg data
// in DISK_SECTORBUF_SIZE (512) byte NETCMD_WRITE chunks. Decoded that's a
// 6-byte header + 512 bytes of payload = 518, and SLIP escaping can double
// that worst-case plus the two frame-delimiter bytes -- 1038 minimum.
// Undersizing this silently truncates every ROM push's WRITE frames, which
// fujibus_transact() and the ESP32 side both see as a timed-out/malformed
// reply rather than anything pointing at the real cause.
#define FUJIBUS_RAW_RX_MAX 1088

#define FUJI_MB_STATUS_IDLE  0
#define FUJI_MB_STATUS_BUSY  1
#define FUJI_MB_STATUS_OK    2
#define FUJI_MB_STATUS_ERR   3

// Intellivision bus-address bounds of the whole mailbox window (header + TX
// + RX). cartridge.c uses these -- not FUJI_MB_BASE, which is a RAM[] index
// -- to decide whether a non-JLP cart should claim this range as RAM.
#define FUJI_MB_ADDR_LO    (0x8000 + FUJI_MB_BASE)
#define FUJI_MB_ADDR_HI    (FUJI_MB_ADDR_LO + 0x140 + FUJI_MB_RX_MAX - 1)

#endif /* FUJI_MAILBOX_H */
