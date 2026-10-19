/* fuji_mailbox_check.h -- fails the build when a cart's fuji_mailbox.h breaks the layout
 * fujimail.c assumes. Included at the end of each cart's fuji_mailbox.h.
 */

#ifndef FUJI_MAILBOX_CHECK_H
#define FUJI_MAILBOX_CHECK_H

#if FN_R_DATA + FN_R_SLICE_LEN != FN_R_BASE || FN_R_SLICE_LEN * FN_R_NSLICES != 1024
#error "fuji_mailbox.h: the reply slices must be 1K just below FN_R_BASE"
#endif

#if FN_R_ACKSEQ != FN_R_BASE || FN_R_STATUS != FN_R_BASE + 0x01 || \
    FN_R_ERR != FN_R_BASE + 0x02 || FN_R_REPLY_CMD != FN_R_BASE + 0x03 || \
    FN_R_RXLEN_LO != FN_R_BASE + 0x04 || FN_R_RXLEN_HI != FN_R_BASE + 0x05 || \
    FN_R_BOOT_STATE != FN_R_BASE + 0x06 || FN_R_BOOT_PCT != FN_R_BASE + 0x07 || \
    FN_R_BOOT_ERR != FN_R_BASE + 0x08 || FN_R_MAGIC0 != FN_R_BASE + 0x09 || \
    FN_R_MAGIC1 != FN_R_BASE + 0x0A || FN_R_PROTO_VER != FN_R_BASE + 0x0B || \
    FN_R_SLICE_ECHO != FN_R_BASE + 0x0C || FN_R_PAINT_END != FN_R_BASE + 0xFC
#error "fuji_mailbox.h: the status page differs from the shared layout"
#endif

#if FN_H_REGSEL != FN_R_BASE + 0x100 || FN_H_REGDATA != FN_R_BASE + 0x200 || \
    FN_H_DATA != FN_R_BASE + 0x300
#error "fuji_mailbox.h: the hotspot pages must follow the status page"
#endif

/* Fields a cart may leave out, but where it has them they sit here. */
#if (defined(FN_R_MODE) && FN_R_MODE != FN_R_BASE + 0x12) || \
    (defined(FN_R_MAPPER) && FN_R_MAPPER != FN_R_BASE + 0x14) || \
    (defined(FN_R_LINK) && FN_R_LINK != FN_R_BASE + 0x15) || \
    (defined(FN_R_BOOT_GOT0) && FN_R_BOOT_GOT0 != FN_R_BASE + 0x16) || \
    (defined(FN_R_BOOT_TOT0) && FN_R_BOOT_TOT0 != FN_R_BASE + 0x19)
#error "fuji_mailbox.h: an optional status field is out of place"
#endif

#endif /* FUJI_MAILBOX_CHECK_H */
