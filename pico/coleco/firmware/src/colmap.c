#include <string.h>

#include "colmap.h"
#include "fuji_mailbox.h"

static bool is_pow2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

/* MegaCart: a whole number of 16K banks, more than two of them (MAME falls
 * back to flat at two or fewer), a power-of-two count so the hotspot mask
 * works, and at most 32 -- the documented ceiling and the largest the mask
 * can express. 64K, 128K, 256K, 512K. */
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
        /* An explicit .cfg is the only route to ACTIVISION or SGC, and a hint
         * that does not fit is a broken .cfg -- say so rather than quietly
         * serving the wrong mapper and letting the game wander. */
        if (!kind_fits(hint, size))
            return COLMAP_ENOMAP;
        kind = hint;
    } else if (size <= COLMAP_WINDOW) {
        kind = COLMAP_FLAT;
    } else if (xin1_shaped(size)) {
        /* Size order matches MAME's get_default_card_software: the 1M/2M
         * X-in-1 test comes before the "bigger than 32K means MegaCart" one. */
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

/* A deliberately forgiving parser: `key=value`, one per line, '#' comments,
 * anything it does not recognise ignored. A .cfg written for some other
 * platform's loader therefore costs nothing rather than failing a boot. */
colmap_kind_t colmap_parse_cfg(const char *text, uint32_t len)
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
    uint32_t i = 0;

    if (text == NULL)
        return COLMAP_KIND_AUTO;

    while (i < len) {
        uint32_t start = i, eol, j;

        while (i < len && text[i] != '\n' && text[i] != '\r')
            i++;
        eol = i;
        while (i < len && (text[i] == '\n' || text[i] == '\r'))
            i++;

        while (start < eol && (text[start] == ' ' || text[start] == '\t'))
            start++;
        if (start == eol || text[start] == '#' || text[start] == ';')
            continue;
        if (eol - start < 7 || memcmp(text + start, "mapper", 6) != 0)
            continue;
        start += 6;
        while (start < eol && (text[start] == ' ' || text[start] == '\t'))
            start++;
        if (start == eol || text[start] != '=')
            continue;
        start++;
        while (start < eol && (text[start] == ' ' || text[start] == '\t'))
            start++;
        while (eol > start && (text[eol - 1] == ' ' || text[eol - 1] == '\t'))
            eol--;

        for (j = 0; j < sizeof names / sizeof names[0]; j++) {
            uint32_t n = (uint32_t)strlen(names[j].name);

            if (eol - start == n && memcmp(text + start, names[j].name, n) == 0)
                return names[j].kind;
        }
    }
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
    /* Only an exactly-32K image can reserve the mailbox pages. A smaller one
     * does not reach FN_R_CLAIM at all (past its end is open bus), and a
     * banked one shows whatever bank is live there, which is not a
     * declaration. */
    if (image == NULL || plan->size != COLMAP_WINDOW)
        return false;
    return memcmp(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
}

void colmap_apply(const uint8_t *image, const colmap_plan_t *plan,
                  uint8_t window[COLMAP_WINDOW])
{
    uint32_t n = plan->size < COLMAP_WINDOW ? plan->size : COLMAP_WINDOW;

    /* The DBC store streams small images straight into the staging window, so
     * `image` and `window` are routinely the same buffer -- copying it onto
     * itself is undefined behaviour for no gain. Only the 0xFF tail matters. */
    if (window != image)
        memcpy(window, image, n);
    if (n < COLMAP_WINDOW)
        memset(window + n, 0xFF, COLMAP_WINDOW - n);
}

void colmap_serve_reset(const colmap_plan_t *plan, colmap_serve_t *s)
{
    memset(s, 0, sizeof *s);
    s->kind = plan->kind;
    s->size = plan->size;
    s->nbanks = plan->nbanks;
    if (plan->kind == COLMAP_XIN1)
        s->win_off = plan->size - COLMAP_WINDOW;   /* MAME: the LAST window */
    colmap_slots_refresh(s);
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
