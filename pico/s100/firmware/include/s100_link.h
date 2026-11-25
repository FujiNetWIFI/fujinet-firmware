/* s100_link.h -- the S-100 card on pico/common's I/O link (fuji_iolink.h): its
 * FDC's track cache, the 2SIO's modem and printer channels, and the LOAD stream
 * that feeds the boot page its images through GETC. */
#ifndef S100_LINK_H
#define S100_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fuji_iolink.h"

/* An image streaming into GETC after CONTROL LOAD. */
typedef struct {
    int img; /* -1 none */
    uint32_t off;
    uint8_t hdr[16];
    int32_t info; /* image offset of the card info, -1 none */
    uint8_t info_bytes[8];
} s100_load_t;

struct s100_card;

void s100_link_init(struct s100_card *card, fuji_tx_fn send, void *ctx);
/* The transport came up or went down (CDC DTR, re-enumeration, TCP). */
void s100_link_set_up(struct s100_card *card, bool connected, uint32_t now);
void s100_link_rx(struct s100_card *card, const uint8_t *buf, size_t len, uint32_t now);
void s100_link_poll(struct s100_card *card, uint32_t now);
/* True while a host request is waiting for its reply. */
bool s100_link_host_busy(struct s100_card *card);

#endif /* S100_LINK_H */
