/* test_cycles.c -- the 1802's bus cycles against the cart.
 *
 * MAME's 1802 makes fewer reads than the chip: CDP1802A Table 2 ("Conditions
 * on data bus and memory address lines during all machine states") has a
 * short branch read M(R(P)) taken or not, IRX read M(R(X)), and IDL read
 * M(R0) every cycle it waits; MAME does none of those. Every other execute
 * cycle Table 2 marks MRD = 1 (GLO, PLO, INC, SEX, ...) makes no read at all,
 * so the console's TPA gate never shows it to the cart.
 *
 * For each trace tools/mktrace.sh recorded in MAME (a .trace in build/traces,
 * stamped with the SHA-1 of the image it ran):
 *   1. every read the chip would make and MAME did not lands on a page with
 *      no side effect -- never a hotspot, never the raster;
 *   2. every read MAME made, through the cart's own s2_bus_read and fujimail,
 *      asks the FujiNet for exactly the transactions the device logged.
 * With no traces (no MAME on the machine) it checks only the table.
 */

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "s2_cart.h"
#include "s2_text.h"
#include "fuji_load.h"
#include "fujimail.h"

/* ---- Table 2, transcribed ----
 * Which register the execute cycle reads memory at, if any. */
enum { RD_NONE, RD_RN, RD_RX, RD_RP, RD_R0, RD_LONG };

static int table2(uint8_t op)
{
    unsigned i = op >> 4, n = op & 15;

    switch (i) {
    case 0x0: return n == 0 ? RD_R0 : RD_RN;            /* IDL; LDN          */
    case 0x1: case 0x2: return RD_NONE;                 /* INC, DEC: Float   */
    case 0x3: return RD_RP;                             /* short branch      */
    case 0x4: return RD_RN;                             /* LDA               */
    case 0x5: return RD_NONE;                           /* STR: a write      */
    case 0x6:                                           /* IRX, OUT; INP     */
        return n <= 7 ? RD_RX : RD_NONE;
    case 0x7:
        switch (n) {
        case 0x0: case 0x1: case 0x2: case 0x4: case 0x5: case 0x7:
            return RD_RX;                               /* RET DIS LDXA ADC SDB SMB */
        case 0xC: case 0xD: case 0xF:
            return RD_RP;                               /* ADCI SDBI SMBI    */
        default:
            return RD_NONE;                             /* STXD SHRC SAV MARK REQ SEQ SHLC */
        }
    case 0x8: case 0x9: case 0xA: case 0xB:
        return RD_NONE;                                 /* GLO GHI PLO PHI   */
    case 0xC: return RD_LONG;                           /* long branch/skip, NOP */
    case 0xD: case 0xE: return RD_NONE;                 /* SEP SEX           */
    default:                                            /* 0xF               */
        if (n == 6 || n == 0xE)
            return RD_NONE;                             /* SHR SHL           */
        return n <= 7 ? RD_RX : RD_RP;                  /* LDX..SM; LDI..SMI */
    }
}

static void test_table(void)
{
    /* spot checks, row by row from the datasheet */
    assert(table2(0x00) == RD_R0 && table2(0x0F) == RD_RN);
    assert(table2(0x1F) == RD_NONE && table2(0x2F) == RD_NONE);
    assert(table2(0x30) == RD_RP && table2(0x3F) == RD_RP);
    assert(table2(0x4F) == RD_RN && table2(0x5F) == RD_NONE);
    assert(table2(0x60) == RD_RX && table2(0x62) == RD_RX && table2(0x69) == RD_NONE);
    assert(table2(0x70) == RD_RX && table2(0x71) == RD_RX && table2(0x73) == RD_NONE);
    assert(table2(0x78) == RD_NONE && table2(0x7A) == RD_NONE && table2(0x7C) == RD_RP);
    assert(table2(0x8F) == RD_NONE && table2(0xAF) == RD_NONE && table2(0xBF) == RD_NONE);
    assert(table2(0xC0) == RD_LONG && table2(0xC4) == RD_LONG);
    assert(table2(0xDF) == RD_NONE && table2(0xEF) == RD_NONE);
    assert(table2(0xF0) == RD_RX && table2(0xF6) == RD_NONE && table2(0xF8) == RD_RP);
    assert(table2(0xFE) == RD_NONE && table2(0xFF) == RD_RP);
}

/* ---- the replay's FujiNet ---- */

static uint8_t arena[FN_ARENA_SIZE];
static s2_text_t text;
static struct { uint8_t dev, cmd, nparam; uint16_t txlen; } txn[64];
static unsigned ntxn;

static void p_poke(unsigned off, uint8_t v) { if (off < FN_ARENA_SIZE) arena[off] = v; }
static bool p_link(void) { return true; }
static fb_status_t p_transact(uint8_t device, uint8_t command, const fb_param_t *params,
                              unsigned nparams, const uint8_t *payload, uint16_t len,
                              uint32_t timeout_ms, fb_reply_t *reply)
{
    (void)params; (void)payload; (void)timeout_ms;
    if (ntxn < 64) {
        txn[ntxn].dev = device;
        txn[ntxn].cmd = command;
        txn[ntxn].nparam = (uint8_t)nparams;
        txn[ntxn].txlen = len;
    }
    ntxn++;
    memset(reply, 0, sizeof *reply);
    reply->device = device;
    reply->command = CMD_FUJI_ACK;
    return FB_OK;
}
static void p_bare(uint8_t d, uint8_t c, const uint8_t *p, uint16_t l) { (void)d; (void)c; (void)p; (void)l; }
static uint8_t p_open(int s, uint32_t n) { (void)s; (void)n; return 0; }
static void p_write(int s, const uint8_t *c, unsigned l) { (void)s; (void)c; (void)l; }
static uint8_t p_close(int s, uint32_t g, bool a) { (void)s; (void)g; (void)a; return 0; }
static void p_arm(void) { }

static const fujimail_port_t port = {
    p_poke, p_link, p_transact, p_bare, p_open, p_write, p_close, p_arm,
    NULL, NULL, NULL, NULL,
};

/* ---- one trace ---- */

static uint8_t img[S2MAP_IMAGE_MAX + 1];
static uint8_t buf0[S2MAP_BUF_MAX], buf1[S2MAP_BUF_MAX];
static fuji_load_t ld;

static char *sha1_of(const char *path)
{
    static char out[64];
    char cmd[1200];
    FILE *p;

    snprintf(cmd, sizeof cmd, "sha1sum '%s' 2>/dev/null", path);
    p = popen(cmd, "r");
    out[0] = 0;
    if (p) {
        if (fscanf(p, "%63s", out) != 1)
            out[0] = 0;
        pclose(p);
    }
    return out;
}

typedef struct { char kind; uint16_t a; uint8_t op, x, p; uint16_t rn, rx; } rec_t;

static int replay(const char *dir, const char *name)
{
    char path[1024], line[256], want_sha[64] = "", image[256] = "";
    static rec_t r[2500000];
    unsigned nr = 0, i, extra = 0, matched = 0, mame_only = 0, pessimistic = 0;
    unsigned want_dev[64], want_cmd[64], want_np[64], want_len[64], nwant = 0;
    const s2_view_t *v;
    s2_bus_t bus;
    uint32_t t = 0;
    FILE *f;
    uint32_t len;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "r");
    assert(f);
    while (fgets(line, sizeof line, f)) {
        unsigned a, op, x, p, rn, rx, d, c, np, tl;

        if (sscanf(line, "# sha1 %63s %255s", want_sha, image) == 2)
            continue;
        if (line[0] == 'X' && sscanf(line, "X %x %x %u %u", &d, &c, &np, &tl) == 4 && nwant < 64) {
            want_dev[nwant] = d; want_cmd[nwant] = c; want_np[nwant] = np; want_len[nwant++] = tl;
            continue;
        }
        if (nr >= sizeof r / sizeof r[0])
            break;
        if (line[0] == 'F' && sscanf(line, "F %x %x %x %x %x %x", &a, &op, &x, &p, &rn, &rx) == 6)
            r[nr++] = (rec_t){ 'F', (uint16_t)a, (uint8_t)op, (uint8_t)x, (uint8_t)p,
                               (uint16_t)rn, (uint16_t)rx };
        else if ((line[0] == 'D' || line[0] == 'M') && sscanf(line + 2, "%x", &a) == 1)
            r[nr++] = (rec_t){ line[0], (uint16_t)a, 0, 0, 0, 0, 0 };
    }
    fclose(f);

    /* the image it ran, and not a rebuilt one */
    snprintf(path, sizeof path, "%s/%s", dir, image);
    if (strcmp(sha1_of(path), want_sha) != 0) {
        fprintf(stderr, "test_cycles: %s is stale (%s changed); run tools/mktrace.sh\n", name, image);
        return 1;
    }
    f = fopen(path, "rb");
    assert(f);
    len = (uint32_t)fread(img, 1, sizeof img, f);
    fclose(f);

    memset(arena, 0, sizeof arena);
    memset(arena + FN_H_REGSEL, 0xFF, FN_ARENA_SIZE - FN_H_REGSEL);
    s2_text_init(&text, arena + FN_R_DATA);
    fuji_load_init(&ld, buf0, buf1, arena, text.raster, img, len);
    v = fuji_load_live(&ld);
    fujimail_init(&port);
    fujimail_paint();
    ntxn = 0;
    s2_bus_reset(&bus);

    for (i = 0; i < nr; i++) {
        uint8_t data;
        unsigned res;

        /* 1. the chip's execute-cycle read, against what MAME did */
        if (r[i].kind == 'F') {
            int how = table2(r[i].op);
            uint16_t hw = 0;
            bool has = true;
            unsigned j = i + 1, n = r[i].op & 15;

            switch (how) {
            case RD_RN: hw = n == r[i].p ? (uint16_t)(r[i].a + 1) : r[i].rn; break;
            case RD_RX: hw = r[i].rx; break;
            case RD_RP: hw = (uint16_t)(r[i].a + 1); break;
            case RD_LONG:
                if (r[i].p == 1 && r[i].op == 0xC0) {
                    /* the ISR's one LBR, before the first DMA; MAME reads
                     * its operands with R(P) already bumped, so the trace
                     * shows them as fetches: step over both */
                    i += 2;
                    continue;
                }
                /* fall through */
            case RD_R0:
                fprintf(stderr, "test_cycles: %s: opcode %02X at $%04X (IDL/3-cycle)\n",
                        name, r[i].op, r[i].a);
                return 1;
            default: has = false; break;
            }
            while (j < nr && r[j].kind == 'M')
                j++;                                    /* DMA may come between */
            if (has) {
                if (j < nr && r[j].kind == 'D' && r[j].a == hw)
                    matched++;
                else {
                    uint8_t ty = v->type[hw >> 8];

                    extra++;
                    if (ty == S2PG_HOT || ty == S2PG_RASTER) {
                        fprintf(stderr, "test_cycles: %s: opcode %02X at $%04X reads $%04X on the "
                                "chip (not in MAME): a %s page\n", name, r[i].op, r[i].a, hw,
                                ty == S2PG_HOT ? "hotspot" : "raster");
                        return 1;
                    }
                }
            } else if (j < nr && r[j].kind == 'D') {
                mame_only++;
            }
            /* what a chip that read R(N) on every cycle would hit */
            if (how == RD_NONE && (r[i].op >> 4) != 0x5 && (r[i].op >> 4) != 0x6 && (r[i].op >> 4) != 0x7) {
                uint8_t ty = v->type[r[i].rn >> 8];

                if (ty == S2PG_HOT)
                    pessimistic++;
            }
        }

        /* 2. MAME's reads through the cart */
        t += r[i].kind == 'M' ? 4 : 5;
        res = s2_bus_read(&bus, v, r[i].a, t, &data);
        if (res & S2_HOT_EV) {
            uint16_t a = r[i].a;

            if (a >= FN_TEXT_BASE)
                s2_text_event(&text, a);
            else
                fujimail_read_hotspot((uint16_t)(a - FN_ARENA_BASE));
        }
        if (res & S2_RESET)
            break;                                      /* a hand-over: the rest is a game */
    }

    if (ntxn != nwant) {
        fprintf(stderr, "test_cycles: %s: %u transactions replayed, MAME's device ran %u\n",
                name, ntxn, nwant);
        return 1;
    }
    for (i = 0; i < nwant && i < 64; i++)
        if (txn[i].dev != want_dev[i] || txn[i].cmd != want_cmd[i] || txn[i].nparam != want_np[i]
            || txn[i].txlen != want_len[i]) {
            fprintf(stderr, "test_cycles: %s: transaction %u is %02X/%02X/%u/%u, MAME's %02X/%02X/%u/%u\n",
                    name, i, txn[i].dev, txn[i].cmd, txn[i].nparam, txn[i].txlen,
                    want_dev[i], want_cmd[i], want_np[i], want_len[i]);
            return 1;
        }
    printf("  %s: %u reads; %u execute reads as MAME, %u the chip adds (none on a hotspot or the "
           "raster), %u MAME adds; %u transactions as logged; %u hotspot hits if /MRD rode "
           "every cycle\n", name, nr, matched, extra, mame_only, ntxn, pessimistic);
    return 0;
}

int main(void)
{
    const char *dir = "../../build/traces";
    DIR *d;
    struct dirent *e;
    int fails = 0, n = 0;

    test_table();
    d = opendir(dir);
    if (d) {
        while ((e = readdir(d)) != NULL) {
            size_t l = strlen(e->d_name);

            if (l > 6 && strcmp(e->d_name + l - 6, ".trace") == 0) {
                fails += replay(dir, e->d_name);
                n++;
            }
        }
        closedir(d);
    }
    if (!n)
        printf("  no traces (tools/mktrace.sh records them in MAME): Table 2 only\n");
    if (fails)
        return 1;
    printf("test_cycles: all passed\n");
    return 0;
}
