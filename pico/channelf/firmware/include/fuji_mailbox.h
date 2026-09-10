/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Fairchild
 * Channel F.
 *
 * Single source of truth, shared by the RP2040 cart firmware, the MAME cart
 * device model, and (hand-mirrored) the F8 console client in
 * testrom/fujilib.inc.
 *
 * WHAT IS DIFFERENT HERE, AND WHY IT MAKES EVERYTHING EASIER
 *
 * The Astrocade, Arcadia and ColecoVision carts push BOTH mailbox directions
 * through the read path, because none of those cartridge edges carries a write
 * strobe. That is where the read-hotspot scheme comes from: the console cannot
 * hand the cart a byte, so it encodes the byte in the low half of an address
 * and performs a read.
 *
 * The Channel F cart is not a ROM. It is a peer on the F8 bus, and ROMC 05 is
 * a store cycle: the console CAN hand it a byte. So console -> cart is an
 * ordinary write, and cart -> console is ordinary memory the cart paints.
 *
 * Two consequences worth stating plainly, because they delete hazards the
 * sibling ports spend real complexity on:
 *
 *   1. Reads of the register and TX pages are INERT. The whole stray-READ
 *      hazard class -- a crashed program fetching through a hotspot page, an
 *      un-maskable vblank NMI landing mid-transaction -- simply does not
 *      exist. The REGSEL/REGDATA arm-and-disarm pairing is kept anyway (see
 *      below), but as protocol shape rather than as a defence.
 *
 *   2. There is no address aliasing to defend against. The F8 gives every
 *      device the full 16-bit register value, so decode is exact -- unlike the
 *      ColecoVision, where A15 never reached the connector and a RAM access at
 *      $7C00-$7FFF was bit-identical to the entire mailbox.
 *
 * The hazard that remains is a stray WRITE, and it has one specific shape:
 * DC0 AUTO-INCREMENTS on every LM and ST. A block copy that runs off the end
 * of the RAM arena walks straight into $FD00 and up, writing registers and
 * streaming garbage into TX. Client-side rule: never let a DC0 run cross
 * $F800 unintentionally, and treat the reply window as the top of RAM.
 *
 * MEMORY MAP (console addresses; the BIOS owns $0000-$07FF)
 *
 *   $0800-$47FF  16K ROM window   client image, or a booted Videocart
 *   $4800-$7FFF  unpopulated      reads $FF
 *   $8000-$F7FF  30K RAM          the client's own read/write memory
 *   $F800-$FBFF  1K reply window  cart-painted, read in place
 *   $FC00-$FCFF  status page      cart-painted; "FUJI" claim at $FCFC
 *   $FD00-$FDFF  register writes  address picks the register, data is the value
 *   $FE00-$FEFF  raw REGDATA      kept so fujimail.c compiles verbatim
 *   $FF00-$FFFF  TX stream        a write anywhere in the page appends its data
 *
 * The ROM window sits at $0800 because that is where the BIOS enters a cart
 * ($55 at $0800, jump to $0802); it cannot be moved. Everything else lives in
 * a 32K "arena" at $8000, whose OFFSETS below are deliberately identical to
 * the ColecoVision port's cart-image offsets -- FN_R_DATA 0x7800, FN_R_BASE
 * 0x7C00, and so on -- so that fujimail.c is copied across unchanged.
 * Console address = FN_ARENA_BASE + offset.
 *
 * THE TX PAGE AND DC0. A write ANYWHERE in $FF00-$FFFF appends its data byte,
 * rather than only a write to $FF00. That is what lets the client set DC0 once
 * with a single DCI and then run STs: DC0 walks $FF00, $FF01, ... and each
 * store appends. One DCI plus N stores, instead of an address computation per
 * byte. The run is 256 stores long; a longer payload re-issues the DCI. (After
 * 256 stores DC0 has wrapped to $0000, which is BIOS ROM and ignores writes,
 * so overrunning is inert rather than destructive.)
 */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* ---------------- address map ---------------- */

#define FN_ROM_BASE      0x0800   /* where the BIOS enters a cartridge       */
#define FN_ROM_SIZE      0x4000   /* 16K client window                       */

#define FN_ARENA_BASE    0x8000   /* RAM + mailbox; offsets below are from here */
#define FN_ARENA_SIZE    0x8000   /* 32K                                     */
#define FN_RAM_TOP       0x7800   /* arena offsets below this are plain RAM  */

/* ---------------- cart -> console: painted memory ---------------- */

/* The whole reply in one piece at $F800-$FBFF, as on the ColecoVision. There
 * the reason was that the console had ~700 usable bytes of RAM; here there is
 * 30K, so it is no longer forced -- but it is still right. Reading a directory
 * entry straight out of the reply window and streaming it back into the next
 * transaction avoids a copy, and FN_R_SLICE_ECHO stays in the register file so
 * fujimail.c compiles verbatim (with one slice it is always 0). */
#define FN_R_DATA        0x7800   /* the whole reply ($F800)                 */
#define FN_R_SLICE_LEN   0x400
#define FN_R_NSLICES     1        /* x 1024 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x7C00   /* first painted status byte ($FC00)       */
#define FN_R_ACKSEQ      0x7C00   /* echoes SEQ when the reply is ready      */
#define FN_R_STATUS      0x7C01   /* bit0 link up, bit1 busy                 */
#define FN_R_ERR         0x7C02   /* fb_status_t of the last transaction     */
#define FN_R_REPLY_CMD   0x7C03   /* 0x06 ACK / 0x15 NAK                     */
#define FN_R_RXLEN_LO    0x7C04   /* total reply length, LE                  */
#define FN_R_RXLEN_HI    0x7C05
#define FN_R_BOOT_STATE  0x7C06
#define FN_R_BOOT_PCT    0x7C07   /* 0-100, for a MOUNT_IMAGE progress bar   */
#define FN_R_BOOT_ERR    0x7C08
#define FN_R_MAGIC0      0x7C09   /* 'F' -- cart presence check              */
#define FN_R_MAGIC1      0x7C0A   /* 'N'                                     */
#define FN_R_PROTO_VER   0x7C0B
#define FN_PROTO_VER     1
#define FN_R_SLICE_ECHO  0x7C0C

#define FN_R_PAINT_END   0x7CFC   /* paint covers [FN_R_BASE, FN_R_PAINT_END) */

/* The claim signature is carried by the cartridge IMAGE, at the very top of
 * the 16K ROM window -- console $47FC. An image with "FUJI" there promises it
 * is a FujiNet client, so the mailbox stays live after it boots. A booted
 * Videocart carries no such claim, and the mailbox goes dead for the session:
 * on this console that also means the arena stops answering at all and
 * $8000-$FFFF reverts to open bus, exactly as a real cart with no RAM fitted.
 *
 * Putting it at the TOP of the window is what makes it safe. Every commercial
 * Videocart is 2K to 6K, so none of them reaches $47FC at all -- past the end
 * of an image is open bus, not a coincidental "FUJI". Client images are
 * therefore always exactly FN_ROM_SIZE bytes; that is the only layout rule,
 * and tools/checkrom.py enforces it. */
#define FN_ROM_CLAIM     (FN_ROM_SIZE - 4)   /* image offset; console $47FC   */
#define FN_R_CLAIM_LEN   4
#define FN_R_CLAIM_SIG   "FUJI"

#define FN_R_STATUS_LINK 0x01
#define FN_R_STATUS_BUSY 0x02

/* Reply-window stability invariant: the reply and RXLEN are repainted ONLY by
 * a SEQ commit or an RXSLICE select. Between those, a client may stream bytes
 * straight out of the reply window into the TX page while building the next
 * transaction, with no bounce buffer. Writes to the TX page do not disturb it. */

/* ---------------- console -> cart: writes ---------------- */

/* A write of value V to FN_H_REGSEL + n (n < 0x80) is one complete register
 * write: the cart synthesises the REGSEL/REGDATA pair that fujimail.c decodes.
 * The pair survives in the protocol because keeping it is free and it lets
 * fujimail.c stay byte-identical to the sibling ports; on this console it is
 * shape, not defence. */
#define FN_H_REGSEL      0x7D00   /* write V to +n: register n = V ($FD00)   */
#define FN_H_REGDATA     0x7E00   /* raw REGDATA half, for the shared decoder */
#define FN_H_DATA        0x7F00   /* write anywhere in page: append data byte */

#define FN_H_PAGE_MASK   0x7F00

/* FN_H_REGSEL offsets 0x80-0xFF are one-shot special operations rather than
 * register numbers, in the bit7-set half as on the Astrocade and Arcadia. */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */

/* Registers, reached by writing to FN_H_REGSEL + n.
 * 0x00-0x0F mirrors the O2/Astrocade/Arcadia/Coleco numbering. */
#define FN_REG_DEVICE    0x00     /* FujiBus device id, e.g. 0x70            */
#define FN_REG_CMD       0x01     /* FujiBus command id                      */
#define FN_REG_NPARAM    0x02     /* number of parameters in the TX stream   */
#define FN_REG_DATA_RST  0x05     /* any value: rewind the TX write pointer  */
#define FN_REG_RXSLICE   0x06     /* which reply slice FN_R_DATA shows       */
#define FN_REG_SEQ       0x10     /* nonzero, != ACKSEQ: launch transaction  */
#define FN_REG_BOOTLOCK  0x11     /* FN_BOOTLOCK_MAGIC: arm the ROM swap     */
#define FN_REG_BOOTSEL_1 0x12     /* FN_BOOTSEL_MAGIC1, then...              */
#define FN_REG_BOOTSEL_2 0x13     /* ...FN_BOOTSEL_MAGIC2: reboot to BOOTSEL */

#define FN_BOOTLOCK_MAGIC 0xB5
#define FN_BOOTSEL_MAGIC1 0xB5
#define FN_BOOTSEL_MAGIC2 0x4A

/* The FN_H_DATA stream is, in order:
 *     NPARAM x { size byte (1|2|4), then that many value bytes, little-endian }
 *     then the raw payload
 * Identical to the O2/Astrocade/Arcadia/Coleco stream. */
#define FN_TX_MAX        320

/* DBC push stream ids, matching lib/media/rs232/diskTypeROM.cpp. Stream 1 is
 * the optional .cfg sibling; the Channel F has no mapper variants to configure
 * so it is accepted and discarded, as on every port but the ColecoVision. */
#define FN_STREAM_ROM    0
#define FN_STREAM_CFG    1

/* FN_R_BOOT_STATE values. */
#define FN_BOOT_IDLE     0
#define FN_BOOT_XFER     1
#define FN_BOOT_READY    2        /* image staged; arm BOOTLOCK, then swap   */
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

/* Booting a staged image, from the client's side:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC (one store to $FD11);
 *   3. copy the swap stub into the RAM arena and jump to it;
 *   4. the stub writes any value to $FDFE (FN_H_REGSEL + FN_HOT_SWAP): the
 *      cart flips the ROM window to the staged image between that store and
 *      the next fetch;
 *   5. the stub jumps to $0000 -- the BIOS cold-starts, re-reads the new
 *      image's own $0800 header and $55 behaves exactly as on a real cart.
 *
 * The stub must run from the RAM arena, because the swap replaces every byte
 * of the ROM window including whatever code triggered it. The Channel F makes
 * this the easiest of the family: there is no VDP to silence and no NMI to
 * hold off, and 30K of RAM to put the stub in.
 *
 * CRITICAL, inherited from the Intellivision/O2/Astrocade/Arcadia/Coleco
 * bring-ups: the client derives its next sequence number from the cart's own
 * persisted FN_R_ACKSEQ + 1 (wrapping 255 -> 1; 0 is reserved as "never
 * used"), never from a program-local counter. A console RESET restarts the
 * client and re-zeroes its variables but does NOT reset the cart. On this
 * console the cart can actually SEE that reset -- it arrives as ROMC 08 -- but
 * the rule stands regardless, because a client can also be restarted by a jump
 * to $0000 that the bus never distinguishes. */

#endif /* FUJI_MAILBOX_H */
