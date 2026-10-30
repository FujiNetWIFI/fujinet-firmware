/* colmap.c -- image planning for the ColecoVision mappers; see colmap.h. */

#include <string.h>

#include "colmap.h"
#include "fuji_mailbox.h"

static bool is_pow2(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

/* MegaCart: 4 to 32 16K banks, a power of two so the hotspot mask works (MAME treats two or
 * fewer as flat): 64K, 128K, 256K, 512K. */
static bool megacart_shaped(uint32_t size)
{
    uint32_t banks = size >> 14;

    return size > COLMAP_WINDOW && (size & 0x3FFF) == 0
        && banks > 2 && banks <= 32 && is_pow2(banks);
}

static bool xin1_shaped(uint32_t size)
{
    return size == COLMAP_XIN1_1M || size == COLMAP_XIN1_2M;
}

/* Activision: a fixed 16K plus up to four 16K banks. */
static bool activision_shaped(uint32_t size)
{
    return size > COLMAP_WINDOW && (size & 0x3FFF) == 0 && (size >> 14) <= 4;
}

/* SGC: a whole number of 8K banks, 1 to 4 Mbit. */
static bool sgc_shaped(uint32_t size)
{
    return size > COLMAP_WINDOW && (size & 0x1FFF) == 0 && size <= 0x80000u;
}

static bool kind_fits(colmap_kind_t kind, uint32_t size)
{
    switch (kind) {
    case COLMAP_FLAT:       return size <= COLMAP_WINDOW;
    case COLMAP_MEGACART:   return megacart_shaped(size);
    case COLMAP_XIN1:       return xin1_shaped(size);
    case COLMAP_ACTIVISION: return activision_shaped(size);
    case COLMAP_SGC:        return sgc_shaped(size);
    default:                return false;
    }
}

static uint16_t bank_count(colmap_kind_t kind, uint32_t size)
{
    switch (kind) {
    case COLMAP_MEGACART:
    case COLMAP_ACTIVISION: return (uint16_t)(size >> 14);
    case COLMAP_XIN1:       return (uint16_t)(size >> 15);
    case COLMAP_SGC:        return (uint16_t)(size >> 13);
    default:                return 0;
    }
}

colmap_err_t colmap_plan(const uint8_t *image, uint32_t size,
                         colmap_kind_t hint, colmap_plan_t *out)
{
    colmap_kind_t kind;

    memset(out, 0, sizeof *out);
    if (size == 0)
        return COLMAP_EEMPTY;

    if (hint != COLMAP_KIND_AUTO) {
        /* A hint that does not fit is a broken .cfg: refuse rather than serve a wrong mapper. */
        if (!kind_fits(hint, size))
            return COLMAP_ENOMAP;
        kind = hint;
    } else if (size <= COLMAP_WINDOW) {
        kind = COLMAP_FLAT;
    } else if (xin1_shaped(size)) {
        /* X-in-1 before MegaCart, as MAME's get_default_card_software tests them. */
        kind = COLMAP_XIN1;
    } else if (megacart_shaped(size)) {
        kind = COLMAP_MEGACART;
    } else {
        return COLMAP_ENOMAP;
    }

    out->size = size;
    out->kind = kind;
    out->nbanks = bank_count(kind, size);
    out->mailbox_ok = colmap_claims_mailbox(image, out);
    return COLMAP_OK;
}

colmap_kind_t colmap_kind_named(const char *name)
{
    static const struct { const char *name; colmap_kind_t kind; } names[] = {
        { "flat",       COLMAP_FLAT },
        { "std",        COLMAP_FLAT },
        { "standard",   COLMAP_FLAT },
        { "megacart",   COLMAP_MEGACART },
        { "xin1",       COLMAP_XIN1 },
        { "activision", COLMAP_ACTIVISION },
        { "sgc",        COLMAP_SGC },
    };
    unsigned idx;

    if (name == NULL)
        return COLMAP_KIND_AUTO;
    for (idx = 0; idx < sizeof names / sizeof names[0]; idx++)
        if (strcmp(name, names[idx].name) == 0)
            return names[idx].kind;
    return COLMAP_KIND_AUTO;
}

uint8_t colmap_gate(uint32_t size)
{
    if (size <= COLMAP_WINDOW            /* incl. 0: older peers omit it */
        || megacart_shaped(size)
        || xin1_shaped(size)
        || activision_shaped(size)
        || sgc_shaped(size))
        return 0;
    return FN_BOOT_ERR_TOOBIG;
}

bool colmap_claims_mailbox(const uint8_t *image, const colmap_plan_t *plan)
{
    if (image == NULL || plan->size != COLMAP_WINDOW)
        return false;
    return memcmp(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
}

void colmap_apply(const uint8_t *image, const colmap_plan_t *plan,
                  uint8_t window[COLMAP_WINDOW])
{
    uint32_t len = plan->size < COLMAP_WINDOW ? plan->size : COLMAP_WINDOW;

    /* The store stages small images in place, and memcpy onto itself is undefined. */
    if (window != image)
        memcpy(window, image, len);
    if (len < COLMAP_WINDOW)
        memset(window + len, 0xFF, COLMAP_WINDOW - len);
}

void colmap_serve_reset(const colmap_plan_t *plan, colmap_serve_t *serve)
{
    memset(serve, 0, sizeof *serve);
    serve->kind = plan->kind;
    serve->size = plan->size;
    serve->nbanks = plan->nbanks;
    if (plan->kind == COLMAP_XIN1)
        serve->win_off = plan->size - COLMAP_WINDOW;   /* MAME: the LAST window */
    colmap_slots_refresh(serve);
}

const char *colmap_strerror(colmap_err_t err)
{
    switch (err) {
    case COLMAP_OK:     return "ok";
    case COLMAP_EEMPTY: return "empty image";
    case COLMAP_ENOMAP: return "no mapper fits";
    default:            return "unknown error";
    }
}

const char *colmap_kindname(colmap_kind_t kind)
{
    switch (kind) {
    case COLMAP_FLAT:       return "flat";
    case COLMAP_MEGACART:   return "megacart";
    case COLMAP_XIN1:       return "xin1";
    case COLMAP_ACTIVISION: return "activision";
    case COLMAP_SGC:        return "sgc";
    default:                return "auto";
    }
}
