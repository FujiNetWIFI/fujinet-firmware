#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

// FujiNet mailbox: Intellivision RAM $9C00-$9F3F, i.e. RAM[0x1C00..0x1F3F]
// (RAM[] is indexed by addr-0x8000). Cells are byte-wide in both directions,
// since console writes are truncated to 8 bits anyway; this keeps jzIntv
// (narrow RAM) and hardware consistent.
//
// It sits at the top of JLP's $8000-$9FFF so $8000-$9BFF stays contiguous for
// JLP RAM. A network-booted JLP game gets the mailbox only if its .cfg [vars]
// sets fujinet = 1.
//
// Handshake: the console fills DEVICE/CMD/NPARAM/params/TX, then bumps SEQ
// last; the RP2040 runs it and writes ACKSEQ = SEQ last. Re-poking the same
// SEQ is a no-op, so a timed-out retry never replays a command.
//
// fujinet-config/intv/fujinet.bas hardcodes these offsets; keep it in sync.

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

// Boot progress, outside the ACKSEQ handshake: the console polls these while
// a long MOUNT_IMAGE (with its mid-transaction ROM push) is outstanding.
#define FUJI_MB_BOOT_STATE (FUJI_MB_BASE + 0x18) // RP2040: FUJI_BOOT_* below
#define FUJI_MB_BOOT_PCT   (FUJI_MB_BASE + 0x19) // RP2040: 0-100
#define FUJI_MB_BOOT_ERR   (FUJI_MB_BASE + 0x1A) // RP2040: reason FUJI_BOOT_FAILED, if it is

// BOOTSEL doorbell: reboots the RP2040 into BOOTSEL so the ESP32-S3 can
// reflash it over USB. Only the exact magic triggers it; there is no ack.
#define FUJI_MB_BOOTSEL_DOORBELL  (FUJI_MB_BASE + 0x1B) // Inty: write FUJI_MB_BOOTSEL_MAGIC to request BOOTSEL
#define FUJI_MB_BOOTSEL_MAGIC     0xB5

#define FUJI_BOOT_IDLE     0
#define FUJI_BOOT_OPENING  1
#define FUJI_BOOT_XFER     2
#define FUJI_BOOT_MAPPING  3
#define FUJI_BOOT_FAILED   0x80

// FUJI_MB_BOOT_ERR reason codes, valid only when BOOT_STATE == FUJI_BOOT_FAILED.
#define FUJI_BOOT_ERR_REJECTED  1 // header magic present but malformed (bad addr range)
#define FUJI_BOOT_ERR_TRUNCATED 2 // stream short/aborted, or tables exceeded receiver limits
#define FUJI_BOOT_ERR_NOMAP     3 // no .cfg/header, and byte count matches no known size
#define FUJI_BOOT_ERR_MAILBOX   4 // a JLP game's segment/RAM overlaps its own $8000-$9FFF window
#define FUJI_BOOT_ERR_CFGBAD    5 // a .cfg stream arrived but had no usable mapping line
#define FUJI_BOOT_ERR_RAM       6 // declared RAM exceeds the cart's RAMSIZE budget
#define FUJI_BOOT_ERR_TOOBIG    7 // ROM larger than this board's cart.ROM[] can stage
#define FUJI_BOOT_ERR_BANKSW    8 // .rom enable tables flag bank-switched banks (unsupported)

#define FUJI_MB_TX         (FUJI_MB_BASE + 0x40)  // Inty: request payload
#define FUJI_MB_TX_MAX     256                    // fujicmd.bas sends fixed 256-byte
                                                   // payloads; do not shrink

#define FUJI_MB_RX         (FUJI_MB_BASE + 0x140) // RP2040: reply payload
#define FUJI_MB_RX_MAX      512                   // largest replies: 256-byte host
                                                   // slots, 240-byte AdapterConfigExtended

// fujibus_transact()'s raw USB receive buffer, unlike FUJI_MB_RX_MAX, must
// hold a whole SLIP-encoded ROM-push WRITE: (6 + 512) bytes doubled by
// escaping, plus 2 delimiters = 1038. Too small truncates every push, which
// shows up only as a timeout.
#define FUJIBUS_RAW_RX_MAX 1088

#define FUJI_MB_STATUS_IDLE  0
#define FUJI_MB_STATUS_BUSY  1
#define FUJI_MB_STATUS_OK    2
#define FUJI_MB_STATUS_ERR   3

// Bus-address bounds of the whole mailbox (FUJI_MB_BASE is a RAM[] index)
#define FUJI_MB_ADDR_LO    (0x8000 + FUJI_MB_BASE)
#define FUJI_MB_ADDR_HI    (FUJI_MB_ADDR_LO + 0x140 + FUJI_MB_RX_MAX - 1)

#endif /* FUJI_MAILBOX_H */
