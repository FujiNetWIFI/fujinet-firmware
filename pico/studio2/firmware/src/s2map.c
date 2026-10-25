/* s2map.c -- the Studio II image planner; see s2map.h. */

#include <string.h>

#include "s2map.h"
#include "fuji_crc.h"

static const uint8_t zero_page[S2MAP_PAGE] = { 0 };
const uint8_t s2map_ff_page[S2MAP_PAGE] = {
#define F16 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
    F16, F16, F16, F16, F16, F16, F16, F16, F16, F16, F16, F16, F16, F16, F16, F16
#undef F16
};

uint8_t s2map_gate(uint32_t size)
{
    return size > S2MAP_IMAGE_MAX ? FN_BOOT_ERR_TOOBIG : 0;
}

static bool is_st2(const uint8_t *buf, uint32_t len)
{
    return len >= 2 * S2MAP_PAGE && memcmp(buf, "RCA2", 4) == 0;
}

/* The byte the plan puts at console address `a`, straight from the buffer. */
static int plan_byte(const uint8_t *buf, uint32_t len, const s2map_plan_t *p, uint16_t a)
{
    unsigned s = p->src[a >> 8];
    uint32_t off;

    if (s == S2SRC_NONE)
        return -1;
    if (s == S2SRC_ZERO)
        return 0;
    off = (s - 2u) * S2MAP_PAGE + (a & 0xFFu);
    return off < len ? buf[off] : 0;
}

int s2map_plan(const uint8_t *buf, uint32_t len, bool force_claim, s2map_plan_t *out)
{
    unsigned pg, i;

    memset(out, 0, sizeof *out);
    out->size = len;
    if (len == 0)
        return S2MAP_EEMPTY;
    if (len > S2MAP_IMAGE_MAX)
        return S2MAP_ETOOBIG;
    out->crc = fuji_crc32(0, buf, len);

    /* Every cart serves $0400-$07FF; MAME's buffer reads zero past the file. */
    for (pg = 0x04; pg <= 0x07; pg++)
        out->src[pg] = S2SRC_ZERO;

    if (is_st2(buf, len)) {
        unsigned blocks = buf[4];
        bool c00 = false;
        unsigned bank = 0;
        bool opened = false;

        out->st2 = true;
        if (blocks < 2)
            return S2MAP_EEMPTY;
        if (blocks > S2MAP_ST2_BLOCKS + 1)
            return S2MAP_ETOOBIG;
        /* Block i's data is buffer page i + 1, whatever page it names: unlike
         * MAME, a skipped block still occupies its place in the file. */
        for (i = 0; i + 1 < blocks; i++) {
            pg = buf[64 + i];
            if (!s2map_page_ok(pg, false)) {
                out->skipped++;
                continue;
            }
            if (pg >= S2MAP_WIN_PAGE && pg < S2MAP_WIN_PAGE + S2MAP_WIN_PAGES) {
                unsigned w = pg - S2MAP_WIN_PAGE, b;

                if (pg == S2MAP_WIN_PAGE) {     /* $C4 opens the next bank */
                    bank += opened;
                    opened = true;
                }
                b = bank;
                if (b >= S2MAP_NBANKS)
                    return S2MAP_ETOOBIG;
                out->bank_src[b][w] = (uint8_t)S2SRC_BUF(i + 1);
                out->nbanks = (uint8_t)(b + 1u);
                if (b) {
                    out->blocks++;
                    continue;           /* src[] holds bank 0 */
                }
            }
            out->src[pg] = (uint8_t)S2SRC_BUF(i + 1);
            out->blocks++;
            if (pg == 0x0C)
                c00 = true;
        }
        /* MAME maps all of $0C00-$0FFF once page $0C is present. */
        if (c00)
            for (pg = 0x0C; pg <= 0x0F; pg++)
                if (out->src[pg] == S2SRC_NONE)
                    out->src[pg] = S2SRC_ZERO;
    } else {
        if (len > S2MAP_RAW_MAX)
            return S2MAP_ETOOBIG;
        for (pg = 0x04; pg <= 0x07; pg++)
            if ((pg - 0x04u) * S2MAP_PAGE < len)
                out->src[pg] = (uint8_t)S2SRC_BUF(pg - 0x04u);
    }

    out->claim = force_claim;
    if (!out->claim) {
        out->claim = true;
        for (i = 0; i < 4; i++)
            if (plan_byte(buf, len, out, (uint16_t)(FN_CLAIM_ADDR + i)) != FN_R_CLAIM_SIG[i])
                out->claim = false;
    }
    if (out->claim)
        for (pg = 0; pg < S2MAP_PAGES; pg++)
            if (out->src[pg] != S2SRC_NONE && !s2map_page_ok(pg, true))
                return S2MAP_EBADPAGE;
    return S2MAP_OK;
}

void s2map_layout(uint8_t *buf, const s2map_plan_t *p)
{
    if (p->size < S2MAP_BUF_MAX)
        memset(buf + p->size, 0, S2MAP_BUF_MAX - p->size);
}

void s2map_view_init(s2_view_t *v, uint8_t *buf, const s2map_plan_t *p,
                     const uint8_t *arena, s2_disp_t *disp, bool mailbox)
{
    unsigned pg, b, w;

    memset(v, 0, sizeof *v);
    v->mailbox = mailbox;
    v->disp = disp;
    for (pg = 0; pg < S2MAP_PAGES; pg++) {
        unsigned s = p->src[pg];

        if (s == S2SRC_NONE)
            continue;
        v->type[pg] = S2PG_ROM;
        if (s == S2SRC_ZERO) {
            v->page[pg] = zero_page;
        } else {
            v->wpage[pg] = buf + (s - 2u) * S2MAP_PAGE;
            v->page[pg] = v->wpage[pg];
        }
    }
    v->nbanks = p->nbanks;
    for (b = 0; b < S2MAP_NBANKS; b++)
        for (w = 0; w < S2MAP_WIN_PAGES; w++) {
            unsigned s = p->bank_src[b][w];

            v->bank_page[b][w] = s2map_ff_page;
            if (s >= S2SRC_BUF(0)) {
                v->bank_wpage[b][w] = buf + (s - 2u) * S2MAP_PAGE;
                v->bank_page[b][w] = v->bank_wpage[b][w];
            }
        }
    if (v->nbanks > 1)
        for (w = 0; w < S2MAP_WIN_PAGES; w++)
            if (v->type[S2MAP_WIN_PAGE + w] == S2PG_NONE)
                v->type[S2MAP_WIN_PAGE + w] = S2PG_ROM;  /* $FF until a bank has it */
    s2map_bank(v, 0);
    if (!mailbox)
        return;
    for (pg = FN_ARENA_BASE >> 8; pg < (FN_ARENA_BASE + FN_ARENA_SIZE) >> 8; pg++) {
        unsigned off = (pg << 8) - FN_ARENA_BASE;

        v->page[pg] = arena + off;
        v->type[pg] = off >= FN_H_REGSEL ? S2PG_HOT : S2PG_ROM;
    }
    for (pg = FN_TEXT_BASE >> 8; pg < (FN_TEXT_BASE + FN_TEXT_SIZE) >> 8; pg++) {
        v->page[pg] = s2map_ff_page;
        v->type[pg] = S2PG_HOT;
    }
    for (pg = FN_RASTER_BASE >> 8; pg < S2MAP_PAGES; pg++) {
        v->page[pg] = zero_page;
        v->type[pg] = S2PG_RASTER;
    }
}

uint8_t s2map_peek(const s2_view_t *v, uint16_t a)
{
    const uint8_t *pg = v->page[a >> 8];

    return pg ? pg[a & 0xFFu] : 0xFF;
}

bool s2map_copy_reg(s2_copy_t *c, s2_view_t *v, uint8_t reg, uint8_t data)
{
    unsigned i;

    switch (reg) {
    case FN_REG_CPSRCLO: c->src = (uint16_t)((c->src & 0xFF00u) | data); return false;
    case FN_REG_CPSRCHI: c->src = (uint16_t)((c->src & 0x00FFu) | (data << 8)); return false;
    case FN_REG_CPDSTLO: c->dst = (uint16_t)((c->dst & 0xFF00u) | data); return false;
    case FN_REG_CPDSTHI: c->dst = (uint16_t)((c->dst & 0x00FFu) | (data << 8)); return false;
    case FN_REG_CPLENLO: c->len = (uint16_t)((c->len & 0xFF00u) | data); return false;
    case FN_REG_CPLENHI:
        c->len = (uint16_t)((c->len & 0x00FFu) | (data << 8));
        for (i = 0; i < c->len; i++)
            (void)s2map_poke(v, (uint16_t)(c->dst + i), s2map_peek(v, (uint16_t)(c->src + i)));
        c->done++;
        return true;
    default:
        return false;
    }
}
