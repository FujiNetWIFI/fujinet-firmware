/* fuji_iolink.h -- an I/O card's one USB CDC link to the ESP32-S3, shared by the
 * host's FujiBus frames from the pipe, the track cache's sector traffic, the
 * printer and the modem.
 *
 * At most one request is outstanding. Host frames go out whole and validated;
 * MODEM_WRITE has no reply on the rs232 bus, so it never waits. Bytes that
 * arrive outside a valid reply frame are modem output; a valid frame nobody is
 * waiting for (a reply that came too late) is dropped. A FUJI command that can
 * change a mount waits until no sector is dirty, and the cache is invalidated
 * after its reply.
 */
#ifndef FUJI_IOLINK_H
#define FUJI_IOLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fuji_pipe.h"
#include "fuji_ring.h"
#include "fuji_tcache.h"
#include "fujibus.h"

#define FUJI_LINK_FRAME_MAX 2304     /* raw, escaped: a 1 KB payload at worst doubles */
#define FUJI_LINK_HOST_US   30000000 /* TNFS mounts and network opens can be slow */
#define FUJI_LINK_OWN_US    2000000
#define FUJI_LINK_ORPHAN_US 2000000
#define FUJI_LINK_PRN_MAX   256 /* the printer emulator's buffer is 320 */
#define FUJI_LINK_PRN_US    50000
#define FUJI_LINK_MDM_MAX   64
#define FUJI_LINK_MDM_US    5000
#define FUJI_LINK_HOLD_US   50000 /* modem bytes after a lone END wait this long for a frame */

typedef size_t (*fuji_tx_fn)(void *ctx, const uint8_t *buf, size_t len);
/* Optional: link events for an emulator's trace (TX, RX, MODEM, TIMEOUT). */
typedef void (*fuji_trace_fn)(const char *event, uint32_t arg1, uint32_t arg2);

/* The card's side of the link; any member may be NULL. */
typedef struct {
    /* Modem output from the ESP32, for the card's serial port. */
    void (*modem_rx)(void *ctx, const uint8_t *buf, uint32_t len);
    /* A from_host entry above a byte, after the link has dropped its own host
     * state; FUJI_PIPE_MARK_FLUSH included. */
    void (*host_mark)(void *ctx, uint16_t mark);
    /* Room in GETC with no reply waiting: the card may stream its own bytes. */
    void (*host_stream)(void *ctx, ring8_t *to_host);
} fuji_iolink_ops_t;

typedef struct {
    fuji_pipe_t *pipe;
    fuji_tcache_t *cache; /* NULL: no disks */
    ring8_t *modem_tx;    /* bus side -> link, NULL: no modem */
    ring8_t *printer_tx;  /* bus side -> link, NULL: no printer */
    const fuji_iolink_ops_t *ops;
    void *ctx;
    fuji_tx_fn send;
    void *send_ctx;
} fuji_iolink_cfg_t;

typedef enum { FUJI_LS_IDLE, FUJI_LS_WAIT, FUJI_LS_ORPHAN } fuji_link_state_t;
typedef enum { FUJI_LO_HOST, FUJI_LO_DISK, FUJI_LO_PRN, FUJI_LO_CARD } fuji_link_owner_t;

typedef struct {
    fuji_iolink_cfg_t cfg;
    fuji_trace_fn trace;
    bool connected;

    /* host frame being assembled from PUTC */
    uint8_t hraw[FUJI_LINK_FRAME_MAX];
    uint16_t hlen;
    bool hin;
    bool hready;
    bool host_last;

    /* reply being assembled from the ESP32 */
    uint8_t rraw[FUJI_LINK_FRAME_MAX];
    uint16_t rlen;
    bool rin;
    uint32_t rin_at;

    fuji_link_state_t state;
    fuji_link_owner_t own;
    uint8_t dev;
    uint32_t deadline;
    bool host_orphaned;
    bool invalidate_after;
    fuji_tc_op_t fop;

    /* a host reply waiting for room in GETC */
    uint8_t hrep[FUJI_LINK_FRAME_MAX];
    uint16_t hrep_len;
    uint16_t hrep_off;

    uint8_t mtx[FUJI_LINK_MDM_MAX];
    uint16_t mlen;
    uint32_t mlast;
    uint8_t ptx[FUJI_LINK_PRN_MAX];
    uint16_t plen;
    uint32_t plast;

    /* a request of the card's own, sent when the link is free */
    bool card_due;
    uint8_t card_dev, card_cmd;
    fb_param_t card_param;
    unsigned card_nparams;
} fuji_iolink_t;

void fuji_iolink_init(fuji_iolink_t *link, const fuji_iolink_cfg_t *cfg);
/* The transport came up or went down (CDC DTR, re-enumeration, TCP). */
void fuji_iolink_set_up(fuji_iolink_t *link, bool connected, uint32_t now);
void fuji_iolink_rx(fuji_iolink_t *link, const uint8_t *buf, size_t len, uint32_t now);
void fuji_iolink_poll(fuji_iolink_t *link, uint32_t now);
/* True while a host request is waiting for its reply. */
bool fuji_iolink_host_busy(const fuji_iolink_t *link);
/* Queues a request of the card's own, with at most one param, ahead of the host's
 * next one; its reply is dropped, and the track cache rereads everything after it,
 * since such a request may change a mount. A second before the first went out
 * replaces it. */
void fuji_iolink_card_request(fuji_iolink_t *link, uint8_t dev, uint8_t cmd,
                              const fb_param_t *param);

#endif /* FUJI_IOLINK_H */
