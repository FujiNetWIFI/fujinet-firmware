/* s100_regs.h -- the card: register dispatch for the pipe, 2SIO, FDC and boot
 * page, shared by the RP firmware (core1 bus side, core0 link side), the SIMH
 * graft and the host tests. */
#ifndef S100_REGS_H
#define S100_REGS_H

#include <stdbool.h>
#include <stdint.h>

#include "s100_2sio.h"
#include "s100_card.h"
#include "s100_fdc.h"
#include "s100_link.h"
#include "s100_ring.h"

#define PIPE_RING 4096

/* from_host entries above a byte */
#define PIPE_MARK_FLUSH 0x100
#define PIPE_MARK_LOAD  0x200 /* | image */

typedef struct {
    const uint8_t *data;
    uint16_t len;
    uint16_t load;
    uint16_t entry;
} s100_image_t;

typedef struct {
    uint8_t pipe_base;
    uint8_t page;
    uint8_t console;
    bool phantom;
    bool poj;
} s100_dip_t;

/* Boot page patch sites: the template is assembled for pipe base A0h and page
 * FFh; each site gets the difference to the real setting added. */
#define S100_PATCH_PIPE 0
#define S100_PATCH_PAGE 1
#define S100_REF_PIPE   0xA0
#define S100_REF_PAGE   0xFF

typedef struct {
    uint8_t kind;
    uint8_t off;
} s100_patch_t;

typedef struct s100_card {
    s100_dip_t dip;
    const s100_image_t *images;
    int nimages;

    /* bus side */
    fdc_t fdc;
    sio2_t sio;
    ring16_t from_host;
    ring8_t to_host;
    uint8_t last_getc;
    uint8_t page[256];
    bool rom_on;
    uint8_t poj_left;
    uint32_t epoch_us; /* TICK origin */

    s100_link_t link;

    uint16_t from_host_buf[PIPE_RING];
    uint8_t to_host_buf[PIPE_RING];
} s100_card_t;

void s100_init(s100_card_t *c, const s100_dip_t *dip, const s100_image_t *images, int nimages,
               const uint8_t tmpl[256], const s100_patch_t *patches, int npatches, uint32_t now);
/* RESET*: page back on, POJ re-armed, FDC deselected, pipe flushed. */
void s100_reset(s100_card_t *c, uint32_t now);

/* bus side: `blk` is the glue's block code, `a` the low address byte */
uint8_t s100_in(s100_card_t *c, uint8_t blk, uint8_t a, uint32_t now);
void s100_out(s100_card_t *c, uint8_t blk, uint8_t a, uint8_t v, uint32_t now);
/* Memory read in the page (or any address while POJ is armed). */
uint8_t s100_mem(s100_card_t *c, uint16_t addr);

/* Fills `hdr` with the LOAD header for image n; returns the image or NULL. */
const s100_image_t *s100_load_header(s100_card_t *c, int n, uint8_t hdr[S100_LOAD_HDR]);

#endif /* S100_REGS_H */
