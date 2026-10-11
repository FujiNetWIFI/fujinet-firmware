/* smsmap.c -- planning and power-on state for the SMS mapper engine.
 *
 * The write path is in smsmap.h, inline, for core1. This file chooses the
 * mapper for an image and sets the state MAME's sega8 devices start in.
 */

#include <string.h>

#include "smsmap.h"
#include "smsmap_db.h"
#include "fuji_mailbox.h"

static const char *const kind_names[SMSMAP_KINDS] = {
    "rom", "codemasters", "korean", "korean_nb", "zemina", "nemesis",
    "janggun", "4pak",
};

uint8_t smsmap_kind_from_name(const char *name)
{
    unsigned k;

    if (name == NULL)
        return SMSMAP_UNSUPPORTED;
    if (strcmp(name, "sega") == 0)
        return SMSMAP_SEGA;
    for (k = 0; k < SMSMAP_KINDS; k++)
        if (strcmp(name, kind_names[k]) == 0)
            return (uint8_t)k;
    return SMSMAP_UNSUPPORTED;
}

const char *smsmap_kind_name(uint8_t kind)
{
    return kind < SMSMAP_KINDS ? kind_names[kind] : "unsupported";
}

uint32_t smsmap_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    static uint32_t table[256];
    static bool ready;
    uint32_t i;

    if (!ready) {
        for (i = 0; i < 256; i++) {
            uint32_t c = i;
            int k;

            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint16_t le16(const uint8_t *img, uint32_t len, uint32_t i)
{
    uint8_t lo = i < len ? img[i] : 0;
    uint8_t hi = i + 1 < len ? img[i + 1] : 0;

    return (uint16_t)(lo | (hi << 8));
}

/* sega8_cart_slot_device::get_cart_type, for the types an SMS slot loads.
 * The SG-1000/SC-3000 types it can also return have no SMS slot option, so
 * MAME refuses those images; here they fall back to the Sega mapper. */
uint8_t smsmap_heuristic(const uint8_t *img, uint32_t len)
{
    uint8_t type = SMSMAP_SEGA;
    uint32_t i;

    if (len >= 0x8000) {
        int _0002 = 0, _8000 = 0, _a000 = 0, _ffff = 0, _3ffe = 0, _4000 = 0, _6000 = 0;

        for (i = 0; i < 0x8000; i++) {
            if (img[i] == 0x32) {
                uint16_t addr = le16(img, len, i + 1);

                if (addr == 0xFFFF) { i += 2; _ffff++; continue; }
                if (addr == 0x0002 || addr == 0x0003 || addr == 0x0004) { i += 2; _0002++; continue; }
                if (addr == 0x8000) { i += 2; _8000++; continue; }
                if (addr == 0xA000) { i += 2; _a000++; continue; }
                if (addr == 0x3FFE) { i += 2; _3ffe++; continue; }
                if (addr == 0x4000) { i += 2; _4000++; continue; }
                if (addr == 0x6000) { i += 2; _6000++; continue; }
            }
        }
        if (len > 0x10000 && (_0002 > _ffff + 2 || (_0002 > 0 && _ffff == 0))) {
            type = SMSMAP_ZEMINA;
            if (len == 0x20000 && img[0] == 0x00 && img[1] == 0x00 && img[2] == 0x00
                && img[0x1E000] == 0xF3 && img[0x1E001] == 0xED && img[0x1E002] == 0x56)
                type = SMSMAP_NEMESIS;
        } else if (_8000 > _ffff + 2 || (_8000 > 0 && _ffff == 0))
            type = SMSMAP_CODEMASTERS;
        else if (_a000 > _ffff + 2 || (_a000 > 0 && _ffff == 0))
            type = SMSMAP_KOREAN;
        else if (_3ffe > _ffff + 2 || _3ffe > 0)
            type = SMSMAP_4PAK;
        else if (_4000 > 0 && _6000 > 0 && _8000 > 0 && _a000 > 0)
            type = SMSMAP_JANGGUN;
    }

    /* Lode Runner (Japan, Europe) trips the Korean test; MAME forces it back. */
    if (len == 0x8000
        && memcmp(img + 0x226C, "LICENSEDFROMBRODERBUND@SOFTWARE@INC", 35) == 0)
        type = SMSMAP_SEGA;
    return type;
}

const smsmap_db_t *smsmap_db_lookup(uint32_t crc)
{
    unsigned lo = 0, hi = smsmap_db_count;

    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;

        if (smsmap_db[mid].crc == crc)
            return &smsmap_db[mid];
        if (smsmap_db[mid].crc < crc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

uint8_t smsmap_gate(uint32_t size)
{
    if (size % 0x4000 == 512)
        size -= 512;
    return size > SMSMAP_IMAGE_MAX ? FN_BOOT_ERR_TOOBIG : 0;
}

int smsmap_plan(const uint8_t *img, uint32_t len, const char *cfg_mapper,
                smsmap_plan_t *out)
{
    const smsmap_db_t *hit;
    uint32_t i;
    uint8_t cfg_kind = smsmap_kind_from_name(cfg_mapper);

    memset(out, 0, sizeof *out);
    if (len == 0)
        return SMSMAP_EEMPTY;
    out->offset = (len % 0x4000 == 512) ? 512 : 0;
    out->size = len - out->offset;
    if (out->size == 0)
        return SMSMAP_EEMPTY;
    out->padded = (out->size + 0x3FFFu) & ~0x3FFFu;
    out->pages = (uint16_t)(out->padded / 0x4000);
    if (out->padded > SMSMAP_IMAGE_MAX)
        return SMSMAP_ETOOBIG;
    img += out->offset;
    out->crc = smsmap_crc32(0, img, out->size);
    out->claim = out->size >= FN_CLAIM_OFFSET + FN_R_CLAIM_LEN
        && memcmp(img + FN_CLAIM_OFFSET, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
    for (i = 0x2000; i <= 0x8000 && !out->header; i <<= 1)
        out->header = out->size >= i && memcmp(img + i - 0x10, "TMR SEGA", 8) == 0;

    /* An unknown image gets what MAME gives one loaded by path: 32K of RAM
     * behind the Sega mapper. A known one gets its software-list entry. */
    if (cfg_kind != SMSMAP_UNSUPPORTED) {
        out->kind = cfg_kind;
        out->ram_size = cfg_kind == SMSMAP_SEGA ? SMSMAP_RAM_MAX : 0;
    } else if ((hit = smsmap_db_lookup(out->crc)) != NULL) {
        if (hit->kind == SMSMAP_UNSUPPORTED)
            return SMSMAP_EUNSUPPORTED;
        out->kind = hit->kind;
        out->ram_size = (uint32_t)hit->ram_kb * 1024u;
    } else {
        out->kind = smsmap_heuristic(img, out->size);
        out->ram_size = out->kind == SMSMAP_SEGA ? SMSMAP_RAM_MAX : 0;
    }

    if (out->kind == SMSMAP_JANGGUN && out->padded > (uint32_t)SMSMAP_REV_BANK * 0x2000)
        return SMSMAP_ETOOBIG;
    if (out->ram_size > SMSMAP_RAM_MAX)
        out->ram_size = SMSMAP_RAM_MAX;
    if (out->padded > (uint32_t)SMSMAP_RAM_BANK * 0x2000)
        out->ram_size = 0;                /* no room left for it */
    return SMSMAP_OK;
}

void smsmap_init(smsmap_t *m, const smsmap_plan_t *plan)
{
    unsigned n = plan->pages, s;

    memset(m, 0, sizeof *m);
    m->plan = *plan;

    switch (plan->kind) {
    case SMSMAP_CODEMASTERS:
        m->bank[0] = 0;
        m->bank[1] = (uint8_t)(1 % n);
        m->bank[2] = 0;
        break;
    case SMSMAP_ZEMINA:
    case SMSMAP_NEMESIS:
        for (s = 0; s < 6; s++)
            m->bank[s] = (uint8_t)(s % (n * 2));
        if (plan->kind == SMSMAP_NEMESIS)
            m->bank[0] = (uint8_t)(n * 2 - 1);
        break;
    case SMSMAP_JANGGUN:
        for (s = 0; s < 6; s++)
            m->bank[s] = (uint8_t)s;
        break;
    case SMSMAP_4PAK:
        break;                            /* its device_reset zeroes all three */
    default:                              /* SEGA, KOREAN, KOREAN_NB */
        m->bank[0] = 0;
        m->bank[1] = (uint8_t)(1 % n);
        m->bank[2] = (uint8_t)(2 % n);
        break;
    }

    switch (plan->kind) {
    case SMSMAP_ZEMINA:
    case SMSMAP_NEMESIS:
        for (s = 0; s < 6; s++)
            smsmap_fill(m, s * 8, 8, m->bank[s]);
        break;
    case SMSMAP_JANGGUN:
        for (s = 0; s < 6; s++)
            smsmap_fill(m, s * 8, 8, smsmap_janggun_bank(m->bank[s]));
        break;
    case SMSMAP_CODEMASTERS:
    case SMSMAP_4PAK:
        for (s = 0; s < 3; s++)
            smsmap_fill16(m, s, m->bank[s], false);
        break;
    default:
        for (s = 0; s < 3; s++)
            smsmap_fill16(m, s, m->bank[s], true);
        break;
    }
}
