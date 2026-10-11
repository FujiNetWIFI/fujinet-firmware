/* a78map.c -- planning an image and building its slots; see a78map.h. */

#include <string.h>

#include "a78map.h"
#include "a78map_db.h"
#include "fuji_mailbox.h"

static const char *const kind_names[A78MAP_KINDS] = {
    "a78_rom", "a78_pokey", "a78_sg", "a78_sg_pokey", "a78_sg_ram",
    "a78_sg9", "a78_mram", "a78_abs", "a78_act", "a78_hsc",
};

uint8_t a78map_kind_from_name(const char *name)
{
    unsigned i;

    for (i = 0; i < A78MAP_KINDS; i++) {
        if (strcmp(name, kind_names[i]) == 0 || strcmp(name, kind_names[i] + 4) == 0)
            return (uint8_t)i;
    }
    return A78MAP_UNSUPPORTED;
}

const char *a78map_kind_name(uint8_t kind)
{
    return kind < A78MAP_KINDS ? kind_names[kind] : "unsupported";
}

uint32_t a78map_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    while (n--) {
        unsigned k;

        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static const a78map_db_t *db_lookup(uint32_t crc)
{
    unsigned lo = 0, hi = a78map_db_count;

    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;

        if (a78map_db[mid].crc == crc)
            return &a78map_db[mid];
        if (a78map_db[mid].crc < crc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

uint8_t a78map_gate(uint32_t size)
{
    return size > A78MAP_IMAGE_MAX + 128 ? FN_BOOT_ERR_TOOBIG : 0;
}

/* MAME a78_cart_slot_device::validate_header, transcribed. */
static unsigned validate_header(unsigned head)
{
    switch (head & 0x3D) {
    case 0x05: case 0x09: case 0x11: case 0x21:
        head &= ~0x01u;
        break;
    case 0x0C: case 0x14: case 0x24:
        head &= ~0x04u;
        break;
    case 0x18: case 0x28:
        head &= ~0x08u;
        break;
    case 0x30:
        head &= ~0x10u;
        break;
    }
    if ((head & 0x3C) && !(head & 0x02))
        head |= 0x02;
    if ((head & 0xFF00) == 0x100 && (head & 0xFF))
        head &= 0xFF00;
    if ((head & 0xFF00) == 0x200 && (head & 0xFF))
        head &= 0xFF00;
    return head;
}

/* The header's cart type (bytes 53-54), decoded as call_load does. */
static uint8_t header_kind(const uint8_t *head, uint32_t len, uint8_t *pokey)
{
    unsigned mapper = validate_header((unsigned)head[53] << 8 | head[54]);
    uint8_t kind = A78MAP_ROM;

    *pokey = A78_POKEY_NONE;
    switch (mapper & 0x2E) {
    case 0x00:
        kind = (mapper & 1) ? A78MAP_POKEY : A78MAP_ROM;
        break;
    case 0x02:
        kind = (mapper & 1) ? A78MAP_SG_POKEY : A78MAP_SG;
        break;
    case 0x06:
        kind = A78MAP_SG_RAM;
        break;
    case 0x0A:
        kind = A78MAP_SG9;
        break;
    case 0x22:
    case 0x26:
        (void)len;
        return A78MAP_UNSUPPORTED;          /* VersaBoard, MegaCart */
    }
    if (mapper & 0x40) {
        if (kind != A78MAP_SG) {
            /* MAME clears the SuperGame bit: SG_POKEY becomes POKEY */
            if (kind == A78MAP_SG_POKEY)
                kind = A78MAP_POKEY;
            *pokey = A78_POKEY_0450;
        }
    }
    if ((mapper & 0xFF00) == 0x0100)
        kind = A78MAP_ACT;
    else if ((mapper & 0xFF00) == 0x0200)
        kind = A78MAP_ABS;
    else if ((mapper & 0x0080) == 0x0080)
        kind = A78MAP_MRAM;
    return kind;
}

static uint8_t size_heuristic(uint32_t size)
{
    if (size <= 0xC000)
        return A78MAP_ROM;
    if (size == 0x24000)
        return A78MAP_SG9;
    return A78MAP_SG;
}

/* MAME rom_alloc: 9 x 16K is the one odd bank count. */
static uint8_t bank_mask(uint32_t size)
{
    uint32_t banks = size / 0x4000;

    if (banks == 0)
        return 0;
    return (uint8_t)((banks & 1) ? banks - 2 : banks - 1);
}

/* "FUJI" at CPU $FF70, read through the image's power-on mapping. */
static bool has_claim(const uint8_t *img, const a78map_plan_t *p)
{
    a78map_t m;
    int32_t off;
    uint32_t i;
    uint8_t b[4];

    a78map_init(&m, p, false);
    for (i = 0; i < 4; i++) {
        off = a78map_sram_read_offset(&m, (uint16_t)(FN_CLAIM_ADDR + i));
        if (off < 0 || (uint32_t)off < p->front
            || (uint32_t)off - p->front >= p->size)
            return false;
        b[i] = img[(uint32_t)off - p->front];
    }
    return memcmp(b, FN_R_CLAIM_SIG, 4) == 0;
}

static void finish(a78map_plan_t *p)
{
    uint32_t span = p->size;

    p->front = 0;
    switch (p->kind) {
    case A78MAP_ROM:
    case A78MAP_POKEY:
    case A78MAP_MRAM:
        /* top-aligned: whole pages, the image ending at $FFFF */
        p->pages = (uint16_t)((span + A78MAP_PAGE_SIZE - 1) / A78MAP_PAGE_SIZE);
        p->front = (uint32_t)p->pages * A78MAP_PAGE_SIZE - span;
        break;
    case A78MAP_HSC:
        /* the 4K ROM sits where A12 puts $3000 */
        p->pages = 1;
        p->front = 0x1000;
        break;
    default:
        p->pages = (uint16_t)((span + A78MAP_PAGE_SIZE - 1) / A78MAP_PAGE_SIZE);
        break;
    }
    p->bank_mask = bank_mask(p->size);
    if ((p->kind == A78MAP_SG_RAM || p->kind == A78MAP_MRAM) && !p->ram_size)
        p->ram_size = A78MAP_RAM_MAX;
    if (p->kind == A78MAP_POKEY || p->kind == A78MAP_SG_POKEY)
        p->pokey |= A78_POKEY_4000;
}

int a78map_plan(const uint8_t *img, uint32_t len, const char *cfg_mapper,
                a78map_plan_t *out)
{
    const a78map_db_t *row;
    uint8_t hkind = A78MAP_UNSUPPORTED, hpokey = A78_POKEY_NONE;
    a78map_plan_t p;

    memset(&p, 0, sizeof p);
    if (len >= 128 && memcmp(img + 1, "ATARI7800", 9) == 0) {
        hkind = header_kind(img, len, &hpokey);
        p.offset = 128;
    }
    p.size = len - p.offset;
    if (p.size == 0)
        return A78MAP_EEMPTY;
    if (p.size > A78MAP_IMAGE_MAX)
        return A78MAP_ETOOBIG;
    p.crc = a78map_crc32(0, img + p.offset, p.size);

    row = db_lookup(p.crc);
    if (row) {
        p.kind = row->kind;
        p.swap8k = (row->flags & A78DB_SWAP8K) != 0;
        p.biosok = row->biosok;
        p.db = true;
        /* an 8K SuperGame board: one 6264, seen twice in $4000-$7FFF */
        if (row->kind == A78MAP_SG_RAM && row->ram_kb == 8)
            p.ram_size = 0x2000;
    } else if (cfg_mapper) {
        p.kind = a78map_kind_from_name(cfg_mapper);
    } else if (hkind != A78MAP_UNSUPPORTED) {
        p.kind = hkind;
        p.pokey = hpokey;
    } else if (p.offset) {
        return A78MAP_EUNSUPPORTED;         /* a header we cannot serve */
    } else {
        p.kind = size_heuristic(p.size);
    }
    if (cfg_mapper && row) {
        uint8_t k = a78map_kind_from_name(cfg_mapper);

        if (k != A78MAP_UNSUPPORTED)
            p.kind = k;                     /* the user's override wins */
    }
    if (p.kind >= A78MAP_KINDS)
        return A78MAP_EUNSUPPORTED;

    finish(&p);
    if (p.pages > A78MAP_IMAGE_MAX / A78MAP_PAGE_SIZE)
        return A78MAP_ETOOBIG;
    p.claim = has_claim(img + p.offset, &p);
    if (p.claim) {
        const uint8_t *flags;
        a78map_t m;
        int32_t off;

        a78map_init(&m, &p, false);
        off = a78map_sram_read_offset(&m, FN_CLAIM_ADDR) - (int32_t)p.front;
        flags = img + p.offset + off;
        if (flags[FN_CLAIM_KIND] && flags[FN_CLAIM_KIND] <= A78MAP_KINDS
            && !row && !cfg_mapper) {
            p.kind = (uint8_t)(flags[FN_CLAIM_KIND] - 1);
            finish(&p);
        }
        if (flags[FN_CLAIM_FLAGS] & FN_CLAIMF_RAM)
            p.ram_size = A78MAP_RAM_MAX;
        p.pokey |= A78_POKEY_0450;          /* every FujiNet app may beep */
    }
    *out = p;
    return A78MAP_OK;
}

void a78map_init(a78map_t *m, const a78map_plan_t *plan, bool hsc)
{
    const a78map_plan_t *p = plan;
    unsigned s;

    m->plan = *plan;
    m->bank = 0;
    m->hsc = hsc;
    for (s = 0; s < A78MAP_SLOTS; s++)
        m->slot[s] = 0;
    if (hsc) {
        m->slot[0] = A78MAP_HSCRAM_PAGE | A78S_ROM_EN | A78S_RAM_EN;
        m->slot[1] = A78MAP_HSCROM_PAGE | A78S_ROM_EN;
    }

    switch (p->kind) {
    case A78MAP_ROM:
    case A78MAP_POKEY:
    case A78MAP_MRAM:
        for (s = 2; s < A78MAP_SLOTS; s++) {
            int first = (int)p->pages - (int)(A78MAP_SLOTS - s);

            m->slot[s] = first >= 0 ? (uint16_t)(a78map_page(m, (unsigned)first) | A78S_ROM_EN)
                                    : (uint16_t)(A78MAP_FF_PAGE | A78S_ROM_EN);
        }
        if (p->kind == A78MAP_POKEY)
            m->slot[2] = m->slot[3] = 0;    /* the POKEY, served by the MCU */
        if (p->kind == A78MAP_MRAM) {
            m->slot[2] = A78MAP_RAM_PAGE | A78S_ROM_EN | A78S_RAM_EN | A78S_A8MASK;
            m->slot[3] = (A78MAP_RAM_PAGE + 1) | A78S_ROM_EN | A78S_RAM_EN | A78S_A8MASK;
        }
        break;
    case A78MAP_SG:
    case A78MAP_SG_POKEY:
    case A78MAP_SG_RAM:
        a78map_put16(m, 2, (unsigned)(p->bank_mask - 1) & 0xFFu);
        a78map_put16(m, 4, 0);
        a78map_put16(m, 6, p->bank_mask);
        if (p->kind == A78MAP_SG_POKEY)
            m->slot[2] = m->slot[3] = 0;
        if (p->kind == A78MAP_SG_RAM) {
            m->slot[2] = A78MAP_RAM_PAGE | A78S_ROM_EN | A78S_RAM_EN;
            m->slot[3] = (A78MAP_RAM_PAGE + (p->ram_size > 0x2000 ? 1 : 0))
                       | A78S_ROM_EN | A78S_RAM_EN;
        }
        break;
    case A78MAP_SG9:
        a78map_put16(m, 2, 0);
        a78map_put16(m, 4, 0);
        a78map_put16(m, 6, (unsigned)p->bank_mask + 1);
        break;
    case A78MAP_ABS:
        a78map_put16(m, 2, 0);
        a78map_put16(m, 4, 2);
        a78map_put16(m, 6, 3);
        break;
    case A78MAP_ACT:
        a78map_put16(m, 2, 6);
        m->slot[4] = (uint16_t)(a78map_page(m, 14) | A78S_ROM_EN);
        m->slot[5] = (uint16_t)(a78map_page(m, 1) | A78S_ROM_EN);
        m->slot[6] = (uint16_t)(a78map_page(m, 0) | A78S_ROM_EN);
        m->slot[7] = (uint16_t)(a78map_page(m, 15) | A78S_ROM_EN);
        break;
    case A78MAP_HSC:
        /* the HSC on its own: an empty child slot reads $FF */
        m->slot[0] = A78MAP_HSCRAM_PAGE | A78S_ROM_EN | A78S_RAM_EN;
        m->slot[1] = 0 | A78S_ROM_EN;
        for (s = 2; s < A78MAP_SLOTS; s++)
            m->slot[s] = A78MAP_FF_PAGE | A78S_ROM_EN;
        break;
    default:
        break;
    }
    /* a claimed app with RAM: 16K at $4000, as SuperGame RAM boards have */
    if (p->claim && p->ram_size && p->kind != A78MAP_SG_RAM && p->kind != A78MAP_MRAM) {
        m->slot[2] = A78MAP_RAM_PAGE | A78S_ROM_EN | A78S_RAM_EN;
        m->slot[3] = (A78MAP_RAM_PAGE + 1) | A78S_ROM_EN | A78S_RAM_EN;
    }
}
