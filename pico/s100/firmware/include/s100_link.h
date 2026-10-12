/* s100_link.h -- the one USB CDC link to the ESP32-S3, shared by the host's
 * FujiBus frames, the FDC's sector traffic, the printer and the modem.
 *
 * At most one request is outstanding. Host frames go out whole and validated;
 * MODEM_WRITE has no reply on the rs232 bus, so it never waits. Bytes that
 * arrive outside a valid reply frame are modem output and go to 2SIO channel A;
 * a valid frame nobody is waiting for (a reply that came too late) is dropped.
 */
#ifndef S100_LINK_H
#define S100_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "s100_fdc.h"

#define LINK_FRAME_MAX 2304 /* raw, escaped: a 1 KB payload at worst doubles */
#define LINK_HOST_US   30000000 /* TNFS mounts and network opens can be slow */
#define LINK_OWN_US    2000000
#define LINK_ORPHAN_US 2000000
#define LINK_PRN_MAX   256 /* the printer emulator's buffer is 320 */
#define LINK_PRN_US    50000
#define LINK_MDM_MAX   64
#define LINK_MDM_US    5000
#define LINK_HOLD_US   50000 /* modem bytes after a lone END wait this long for a frame */

#define FB_DEV_FUJI    0x70
#define FB_DEV_DISK    0x31
#define FB_DEV_PRINTER 0x40
#define FB_DEV_MODEM   0x50
#define FB_DEV_DBC     0xFF

typedef size_t (*s100_tx_fn)(void *ctx, const uint8_t *buf, size_t n);
/* Optional: link events for an emulator's trace (TX, RX, MODEM, TIMEOUT). */
typedef void (*s100_trace_fn)(const char *ev, uint32_t a, uint32_t b);

typedef enum { LS_IDLE, LS_WAIT, LS_ORPHAN } link_state_t;
typedef enum { LO_HOST, LO_FDC, LO_PRN } link_owner_t;

typedef struct {
    s100_tx_fn tx;
    void *tx_ctx;
    s100_trace_fn trace;
    bool up;

    /* host frame being assembled from PUTC */
    uint8_t hraw[LINK_FRAME_MAX];
    uint16_t hlen;
    bool hin;
    bool hready;
    bool host_last;

    /* reply being assembled from the ESP32 */
    uint8_t rraw[LINK_FRAME_MAX];
    uint16_t rlen;
    bool rin;
    uint32_t rin_at;

    link_state_t st;
    link_owner_t own;
    uint8_t dev;
    uint32_t deadline;
    bool host_orphaned;
    bool invalidate_after;
    fdc_op_t fop;

    /* a host reply waiting for room in GETC */
    uint8_t hrep[LINK_FRAME_MAX];
    uint16_t hrep_len;
    uint16_t hrep_off;

    /* LOAD stream */
    int load_img; /* -1 none */
    uint32_t load_off;
    uint8_t load_hdr[16];
    int32_t load_info; /* image offset of the card info, -1 none */
    uint8_t info[8];

    uint8_t mtx[LINK_MDM_MAX];
    uint16_t mlen;
    uint32_t mlast;
    uint8_t ptx[LINK_PRN_MAX];
    uint16_t plen;
    uint32_t plast;
} s100_link_t;

struct s100_card;

void s100_link_init(struct s100_card *c, s100_tx_fn tx, void *ctx);
/* The transport came up or went down (CDC DTR, re-enumeration, TCP). */
void s100_link_set_up(struct s100_card *c, bool up, uint32_t now);
void s100_link_rx(struct s100_card *c, const uint8_t *buf, size_t n, uint32_t now);
void s100_link_poll(struct s100_card *c, uint32_t now);
/* True while a host request is waiting for its reply. */
bool s100_link_host_busy(struct s100_card *c);

#endif /* S100_LINK_H */
