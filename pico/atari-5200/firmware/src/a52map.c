/* a52map.c -- planning an image and laying out its view; see a52map.h. */

#include <string.h>

#include "a52map.h"
#include "a52map_db.h"
#include "fuji_mailbox.h"

static const char *const kind_names[A52MAP_KINDS] = {
    "a5200_rom", "a5200_2chips", "a5200_bbsb", "a5200_supercart",
};

uint8_t a52map_kind_from_name(const char *name)
{
    unsigned i;

    for (i = 0; i < A52MAP_KINDS; i++) {
        if (strcmp(name, kind_names[i]) == 0 || strcmp(name, kind_names[i] + 6) == 0)
            return (uint8_t)i;
    }
    return A52MAP_UNSUPPORTED;
}

const char *a52map_kind_name(uint8_t kind)
{
    return kind < A52MAP_KINDS ? kind_names[kind] : "unsupported";
}

uint32_t a52map_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
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

static const a52map_db_t *db_lookup(uint32_t crc)
{
    unsigned lo = 0, hi = a52map_db_count;

    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;

        if (a52map_db[mid].crc == crc)
            return &a52map_db[mid];
        if (a52map_db[mid].crc < crc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

uint8_t a52map_gate(uint32_t size)
{
    return size > A52MAP_IMAGE_MAX + A52MAP_CAR_HEADER ? FN_BOOT_ERR_TOOBIG : 0;
}

/* The .car types MAME's a5200 slot knows, plus atari800's alternate Bounty
 * Bob layout (159) and the Super Cart (71-74). */
static uint8_t car_kind(uint32_t type, bool *car7)
{
    *car7 = false;
    switch (type) {
    case 4: case 16: case 19: case 20:
        return A52MAP_ROM;
    case 6:
        return A52MAP_2CHIPS;
    case 7:
        *car7 = true;
        return A52MAP_BBSB;
    case 159:
        return A52MAP_BBSB;
    case 71: case 72: case 73: case 74:
        return A52MAP_SUPERCART;
    default:
        return A52MAP_UNSUPPORTED;
    }
}

static bool pow2(uint32_t n)
{
    return n && !(n & (n - 1));
}

/* The kind's demands on the image; false if it cannot be served so. */
static bool fits(uint8_t kind, uint32_t size)
{
    switch (kind) {
    case A52MAP_ROM:
        return size > 0;
    case A52MAP_2CHIPS:
        return size >= 0x4000;
    case A52MAP_BBSB:
        return size == A52MAP_BBSB_SIZE;
    case A52MAP_SUPERCART:
        return size >= 2 * A52MAP_WINDOW && size <= A52MAP_IMAGE_MAX
            && size % A52MAP_WINDOW == 0 && pow2(size / A52MAP_WINDOW);
    default:
        return false;
    }
}

static uint8_t size_kind(const uint8_t *img, uint32_t size, uint8_t *src)
{
    *src = A52SRC_SIZE;
    if (size == 0x4000) {
        *src = A52SRC_TRACE;
        return a52map_guess16k(img);
    }
    if (size == A52MAP_BBSB_SIZE)
        return A52MAP_BBSB;
    if (size > A52MAP_WINDOW)
        return fits(A52MAP_SUPERCART, size) ? A52MAP_SUPERCART : A52MAP_UNSUPPORTED;
    return A52MAP_ROM;
}

/* Image byte at CPU address a ($4000-$BFFF) in the kind's power-on state, or
 * -1 where nothing in the image answers. */
static int32_t power_on_offset(uint8_t kind, uint32_t size, bool car7, uint16_t a)
{
    uint32_t off = (uint32_t)a - FN_WINDOW_BASE;

    switch (kind) {
    case A52MAP_ROM:
        return (int32_t)(off & (size - 1));
    case A52MAP_2CHIPS:
        return (int32_t)((off & 0x4000u ? 0x2000u : 0) + (off & 0x1FFFu));
    case A52MAP_BBSB:
        if (off < 0x2000u)
            return (int32_t)((off < 0x1000u ? 0x2000u : 0x6000u) + (off & 0x0FFFu)
                             - (car7 ? 0x2000u : 0));
        if (off < 0x4000u)
            return -1;
        return (int32_t)((off & 0x1FFFu) + (car7 ? 0x8000u : 0));
    case A52MAP_SUPERCART:
        return (int32_t)(size - A52MAP_WINDOW + off);
    default:
        return -1;
    }
}

static bool has_claim(const uint8_t *img, const a52map_plan_t *p)
{
    uint8_t b[4];
    unsigned i;

    for (i = 0; i < 4; i++) {
        int32_t off = power_on_offset(p->kind, p->size, p->car7,
                                      (uint16_t)(FN_CLAIM_ADDR + i));

        if (off < 0 || (uint32_t)off >= p->size)
            return false;
        b[i] = img[off];
    }
    return memcmp(b, FN_R_CLAIM_SIG, 4) == 0;
}

static void finish(a52map_plan_t *p)
{
    switch (p->kind) {
    case A52MAP_BBSB:
        p->view_len = A52MAP_BBSB_ZERO + A52MAP_PAGE;
        p->nbanks = 0;
        break;
    case A52MAP_SUPERCART:
        p->nbanks = (uint8_t)(p->size / A52MAP_WINDOW);
        p->view_len = p->nbanks * (A52MAP_WINDOW + A52MAP_PAGE);
        break;
    default:
        p->view_len = A52MAP_WINDOW;
        p->nbanks = 0;
        break;
    }
}

int a52map_plan(const uint8_t *img, uint32_t len, const char *cfg_mapper,
                a52map_plan_t *out)
{
    const a52map_db_t *row;
    uint8_t ckind = A52MAP_UNSUPPORTED, k;
    a52map_plan_t p;

    memset(&p, 0, sizeof p);
    if (len % 0x1000u == A52MAP_CAR_HEADER && memcmp(img, "CART", 4) == 0) {
        uint32_t type = (uint32_t)img[4] << 24 | (uint32_t)img[5] << 16
                      | (uint32_t)img[6] << 8 | img[7];

        ckind = car_kind(type, &p.car7);
        if (ckind == A52MAP_UNSUPPORTED)
            return A52MAP_EUNSUPPORTED;     /* a header we cannot serve */
        p.offset = A52MAP_CAR_HEADER;
    }
    p.size = len - p.offset;
    if (p.size == 0)
        return A52MAP_EEMPTY;
    if (p.size > A52MAP_IMAGE_MAX)
        return A52MAP_ETOOBIG;
    img += p.offset;
    p.crc = a52map_crc32(0, img, p.size);

    row = db_lookup(p.crc);
    if (row) {
        p.kind = row->kind;
        p.src = A52SRC_DB;
    } else if (ckind != A52MAP_UNSUPPORTED) {
        p.kind = ckind;
        p.src = A52SRC_CAR;
    } else {
        p.kind = size_kind(img, p.size, &p.src);
    }
    if (cfg_mapper && (k = a52map_kind_from_name(cfg_mapper)) != A52MAP_UNSUPPORTED
        && fits(k, p.size)) {
        p.kind = k;                         /* the user's override wins */
        p.src = A52SRC_CFG;
        p.car7 = p.car7 && k == A52MAP_BBSB;
    }
    if (!fits(p.kind, p.size))
        return p.kind == A52MAP_SUPERCART && p.size > A52MAP_IMAGE_MAX
            ? A52MAP_ETOOBIG : A52MAP_EUNSUPPORTED;

    p.claim = (p.kind == A52MAP_ROM || p.kind == A52MAP_SUPERCART) && has_claim(img, &p);
    if (p.claim && p.src != A52SRC_DB && p.src != A52SRC_CFG && p.src != A52SRC_CAR) {
        int32_t f = power_on_offset(p.kind, p.size, false, FN_CLAIM_ADDR);
        uint8_t ck = img[f + FN_CLAIM_KIND];

        if (ck && ck <= A52MAP_KINDS && fits((uint8_t)(ck - 1), p.size)) {
            p.kind = (uint8_t)(ck - 1);
            p.src = A52SRC_CLAIM;
            p.claim = (p.kind == A52MAP_ROM || p.kind == A52MAP_SUPERCART)
                   && has_claim(img, &p);
        }
    }
    finish(&p);
    *out = p;
    return A52MAP_OK;
}

static void reverse(uint8_t *a, uint32_t n)
{
    uint32_t i, j;

    for (i = 0, j = n; i < j--; i++) {
        uint8_t t = a[i];

        a[i] = a[j];
        a[j] = t;
    }
}

void a52map_layout(uint8_t *buf, const a52map_plan_t *p)
{
    uint32_t i, b;

    if (p->offset)
        memmove(buf, buf + p->offset, p->size);

    switch (p->kind) {
    case A52MAP_ROM:
        /* every source index is below size, so filling upward is safe */
        for (i = p->size; i < A52MAP_WINDOW; i++)
            buf[i] = buf[i & (p->size - 1)];
        break;
    case A52MAP_2CHIPS:
        memcpy(buf + 0x4000, buf + 0x2000, 0x2000);
        memcpy(buf + 0x6000, buf + 0x2000, 0x2000);
        memcpy(buf + 0x2000, buf, 0x2000);
        break;
    case A52MAP_BBSB:
        if (p->car7) {
            /* atari800 type 7 keeps the fixed 8K last: rotate it to the front */
            reverse(buf, A52MAP_BBSB_SIZE);
            reverse(buf, 0x2000);
            reverse(buf + 0x2000, A52MAP_BBSB_SIZE - 0x2000);
        }
        /* MAME answers $FF at a hotspot whatever the bank */
        for (b = 0; b < 4; b++) {
            memset(buf + 0x2000u + b * 0x1000u + 0x0FF6u, 0xFF, 4);
            memset(buf + 0x6000u + b * 0x1000u + 0x0FF6u, 0xFF, 4);
        }
        memset(buf + A52MAP_BBSB_ZERO, 0, A52MAP_PAGE);
        break;
    case A52MAP_SUPERCART:
        for (b = 0; b < p->nbanks; b++) {
            uint8_t *v = buf + p->nbanks * A52MAP_WINDOW + b * A52MAP_PAGE;

            memcpy(v, buf + b * A52MAP_WINDOW + 15u * A52MAP_PAGE, A52MAP_PAGE);
            for (i = 0x7FC0u; i < A52MAP_WINDOW; i++) {
                uint8_t nb = a52map_sc_bank((uint8_t)b, i, (uint8_t)(p->nbanks - 1));

                v[i & (A52MAP_PAGE - 1)] = buf[nb * A52MAP_WINDOW + i];
            }
        }
        break;
    default:
        break;
    }
}

void a52map_view_init(a52_view_t *v, const uint8_t *base, const a52map_plan_t *p,
                      const uint8_t *arena, bool mailbox)
{
    memset(v, 0, sizeof *v);
    v->kind = p->kind;
    v->nbanks = p->nbanks;
    v->base = base;
    v->arena = arena;
    v->mailbox = mailbox;
    if (p->kind == A52MAP_SUPERCART)
        v->bank[0] = (uint8_t)(p->nbanks - 1);
    if (mailbox)
        v->hot |= 1u << A52MAP_ARENA_PAGE;
    if (p->kind == A52MAP_BBSB)
        v->hot |= (1u << 1) | (1u << 3);
    if (p->kind == A52MAP_SUPERCART)
        v->hot |= 1u << 15;
    a52map_view_pages(v);
}

/* ---- the 16K guess ----
 *
 * The two mappings agree on $4000-$5FFF and $A000-$BFFF and differ at
 * $6000-$9FFF. Code followed from the reset vector under the wrong one soon
 * dies: it runs into an opcode no 6502 program uses, jumps into the chips at
 * $C000-$EFFF, or stops after a handful of instructions. */

#define OP_ILL  0       /* undocumented: a wrong turn                        */
#define OP_SEQ  1       /* falls through                                     */
#define OP_BRA  2       /* relative branch                                   */
#define OP_JMP  3       /* JMP abs                                           */
#define OP_JSR  4
#define OP_END  5       /* RTS, RTI, JMP (ind): the trace ends here          */
#define OP_BRK  6

/* NMOS 6502 documented opcodes: length in the low nibble, class above. */
static uint8_t opinfo(uint8_t op)
{
    static const uint8_t len[256] = {
        /*0*/ 1,2,0,0,0,2,2,0,1,2,1,0,0,3,3,0,
        /*1*/ 2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
        /*2*/ 3,2,0,0,2,2,2,0,1,2,1,0,3,3,3,0,
        /*3*/ 2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
        /*4*/ 1,2,0,0,0,2,2,0,1,2,1,0,3,3,3,0,
        /*5*/ 2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
        /*6*/ 1,2,0,0,0,2,2,0,1,2,1,0,3,3,3,0,
        /*7*/ 2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
        /*8*/ 0,2,0,0,2,2,2,0,1,0,1,0,3,3,3,0,
        /*9*/ 2,2,0,0,2,2,2,0,1,3,1,0,0,3,0,0,
        /*A*/ 2,2,2,0,2,2,2,0,1,2,1,0,3,3,3,0,
        /*B*/ 2,2,0,0,2,2,2,0,1,3,1,0,3,3,3,0,
        /*C*/ 2,2,0,0,2,2,2,0,1,2,1,0,3,3,3,0,
        /*D*/ 2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
        /*E*/ 2,2,0,0,2,2,2,0,1,2,1,0,3,3,3,0,
        /*F*/ 2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
    };
    uint8_t n = len[op];

    if (n == 0)
        return OP_ILL << 4;
    if (op == 0x00)
        return (uint8_t)(OP_BRK << 4 | 1);
    if (op == 0x4C)
        return (uint8_t)(OP_JMP << 4 | 3);
    if (op == 0x20)
        return (uint8_t)(OP_JSR << 4 | 3);
    if (op == 0x60 || op == 0x40 || op == 0x6C)
        return (uint8_t)(OP_END << 4 | n);
    if ((op & 0x1F) == 0x10)
        return (uint8_t)(OP_BRA << 4 | 2);
    return (uint8_t)(OP_SEQ << 4 | n);
}

static int peek16(const uint8_t *img, uint8_t kind, uint32_t a)
{
    uint32_t off;

    if (a < FN_WINDOW_BASE || a >= FN_WINDOW_BASE + A52MAP_WINDOW)
        return -1;
    off = a - FN_WINDOW_BASE;
    if (kind == A52MAP_2CHIPS)
        return img[(off & 0x4000u ? 0x2000u : 0) + (off & 0x1FFFu)];
    return img[off & 0x3FFFu];
}

#define TRACE_MAX 4096

typedef struct {
    unsigned reached;       /* distinct instructions followed                */
    unsigned bad;           /* undocumented opcodes, BRKs, jumps into chips  */
} trace_t;

static trace_t trace(const uint8_t *img, uint8_t kind)
{
    static uint8_t seen[A52MAP_WINDOW / 8];
    uint16_t todo[256];
    unsigned ntodo = 0;
    trace_t t = { 0, 0 };
    uint32_t pc;

    memset(seen, 0, sizeof seen);
    todo[ntodo++] = (uint16_t)(img[0x3FFE] | img[0x3FFF] << 8);
    while (ntodo && t.reached < TRACE_MAX) {
        pc = todo[--ntodo];
        for (;;) {
            int op = peek16(img, kind, pc);
            uint8_t info, cls;
            uint32_t off;

            if (op < 0)
                break;                  /* RAM or BIOS: not ours to judge */
            off = pc - FN_WINDOW_BASE;
            if (seen[off >> 3] & (1u << (off & 7)))
                break;
            seen[off >> 3] |= (uint8_t)(1u << (off & 7));
            if (++t.reached >= TRACE_MAX)
                break;
            info = opinfo((uint8_t)op);
            cls = info >> 4;
            if (cls == OP_ILL || cls == OP_BRK) {
                t.bad++;
                break;
            }
            if (cls == OP_END)
                break;
            if (cls == OP_JMP || cls == OP_JSR) {
                int lo = peek16(img, kind, pc + 1), hi = peek16(img, kind, pc + 2);
                uint16_t to;

                if (lo < 0 || hi < 0)
                    break;
                to = (uint16_t)(lo | hi << 8);
                if (to >= 0xC000u && to < 0xF000u)
                    t.bad++;
                if (ntodo < sizeof todo / sizeof todo[0])
                    todo[ntodo++] = to;
                if (cls == OP_JMP)
                    break;
            } else if (cls == OP_BRA) {
                int d = peek16(img, kind, pc + 1);

                if (d < 0)
                    break;
                if (ntodo < sizeof todo / sizeof todo[0])
                    todo[ntodo++] = (uint16_t)(pc + 2 + (int8_t)d);
            }
            pc += info & 15u;
        }
    }
    return t;
}

#define TRACE_DEAD   128    /* a trace this short died at once               */
#define TRACE_ALIVE  512
#define TRACE_COST   512    /* what one wrong turn is worth in instructions  */

uint8_t a52map_guess16k(const uint8_t *img)
{
    trace_t lin = trace(img, A52MAP_ROM);
    trace_t two = trace(img, A52MAP_2CHIPS);
    long sl, s2;

    if (lin.reached < TRACE_DEAD && two.reached >= TRACE_ALIVE)
        return A52MAP_2CHIPS;
    if (two.reached < TRACE_DEAD && lin.reached >= TRACE_ALIVE)
        return A52MAP_ROM;
    sl = (long)lin.reached - TRACE_COST * (long)lin.bad;
    s2 = (long)two.reached - TRACE_COST * (long)two.bad;
    /* a tie is code that never leaves the shared halves: most such boards
     * are 2-chip */
    return sl > s2 ? A52MAP_ROM : A52MAP_2CHIPS;
}
