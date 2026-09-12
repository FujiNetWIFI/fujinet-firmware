/* test_vcsmap.c -- the cartridge mappers against MAME's own handlers.
 *
 * The reference below is a VERBATIM TRANSCRIPTION of
 * src/devices/bus/vcs/rom.cpp: the same ranges, the same bank entries, the
 * same order of read-then-tap. It is deliberately written in MAME's shape
 * rather than tidied into ours, because the point is to disagree with
 * vcsmap.h wherever vcsmap.h is wrong -- and a reference refactored to look
 * like the code under test stops being able to.
 *
 * MAME's read taps run AFTER the read they are attached to
 * (emumem_het.cpp: `data = m_next->read(...); m_tap(offset, data, mem_mask);`)
 * so a bankswitch hotspot returns the byte from the bank that was live BEFORE
 * it. The reference models that literally.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vcsmap.h"
#include "vcs_cart.h"

static int fails;

/* --------------------------------------------------------------------------
 * The reference. One struct, one read, one write, per MAME's device.
 * -------------------------------------------------------------------------- */
typedef struct {
    const uint8_t *rom;
    uint32_t rom_size;
    int entry;                  /* m_bank->set_entry() */
    int e0[3];                  /* m_bank[0..2] */
    int superchip;
    uint8_t ram[1024];
    int fe_trigger;             /* m_trigger_on_next_access */
    int fe_ignore;              /* m_ignore_first_read */
} ref_t;

/* install_rom(start, end, mirror, base): MAME folds the mirror bits out of
 * the address before indexing. */
static uint8_t rom_at(const ref_t *r, uint32_t off)
{
    return r->rom[off % r->rom_size];
}

static int ref_read(ref_t *r, vcsmap_kind_t k, uint16_t a, uint8_t bus, int commit)
{
    int data = -1;

    if (a & 0x1000) {
        switch (k) {
        case VCSMAP_FLAT:
            /* 2K: install_rom(0x1000,0x17ff, 0x800, base). 4K: 0x1000-0x1fff. */
            data = rom_at(r, r->rom_size < 0x1000 ? (a & 0x7FF) : (a & 0xFFF));
            break;
        case VCSMAP_CV:
            /* install_rom(0x1000,0x17ff,0x800,base), then
             * install_read_handler(0x1000,0x13ff, read_ram). */
            if (a >= 0x1000 && a <= 0x13FF) data = r->ram[a - 0x1000];
            else                            data = rom_at(r, a & 0x7FF);
            break;
        case VCSMAP_F8: case VCSMAP_F6: case VCSMAP_F4: case VCSMAP_FA:
            if (k == VCSMAP_FA && a >= 0x1100 && a <= 0x11FF)
                data = r->ram[a - 0x1100];      /* install_read_handler(0x1100,0x11ff) */
            else if (k != VCSMAP_FA && r->superchip && a >= 0x1080 && a <= 0x10FF)
                data = r->ram[a - 0x1080];      /* install_super_chip_handlers */
            else
                data = rom_at(r, (uint32_t)r->entry * 0x1000 + (a & 0xFFF));
            break;
        case VCSMAP_E0:
            if (a <= 0x13FF)      data = rom_at(r, (uint32_t)r->e0[0] * 0x400 + (a & 0x3FF));
            else if (a <= 0x17FF) data = rom_at(r, (uint32_t)r->e0[1] * 0x400 + (a & 0x3FF));
            else if (a <= 0x1BFF) data = rom_at(r, (uint32_t)r->e0[2] * 0x400 + (a & 0x3FF));
            else                  data = rom_at(r, 7u * 0x400 + (a & 0x3FF));
            break;
        case VCSMAP_UA: case VCSMAP_FE:
            data = rom_at(r, (uint32_t)r->entry * 0x1000 + (a & 0xFFF));
            break;
        default:
            break;
        }
    }

    if (!commit)
        return data;

    /* ---- the taps, which run after the read ---- */
    switch (k) {
    case VCSMAP_F8:
        if (a >= 0x1FF8 && a <= 0x1FF9) r->entry = a - 0x1FF8;
        break;
    case VCSMAP_F6:
        if (a >= 0x1FF6 && a <= 0x1FF9) r->entry = a - 0x1FF6;
        break;
    case VCSMAP_F4:
        if (a >= 0x1FF4 && a <= 0x1FFB) r->entry = a - 0x1FF4;
        break;
    case VCSMAP_FA:
        if (a >= 0x1FF8 && a <= 0x1FFA) r->entry = a - 0x1FF8;
        break;
    case VCSMAP_E0:
        if (a >= 0x1FE0 && a <= 0x1FE7)      r->e0[0] = a & 7;
        else if (a >= 0x1FE8 && a <= 0x1FEF) r->e0[1] = a & 7;
        else if (a >= 0x1FF0 && a <= 0x1FF7) r->e0[2] = a & 7;
        break;
    case VCSMAP_UA:
        /* install_readwrite_tap(0x200, 0x27f): change_bank(offset) with
         * set_entry((offset >> 6) & 1). `offset` here is the raw address. */
        if (a >= 0x200 && a <= 0x27F) r->entry = (a >> 6) & 1;
        break;
    case VCSMAP_FE:
        /* switch_bank(data) on a read at 0x1ff or anywhere in 0x1000-0x1fff;
         * trigger_bank() on an access to 0x1fe. */
        if ((a == 0x1FF || (a & 0x1000)) && r->fe_trigger) {
            uint8_t d = (data >= 0) ? (uint8_t)data : bus;
            r->entry = (d & 0x20) ? 0 : 1;
            r->fe_trigger = 0;
        }
        if (a == 0x1FE) {
            if (r->fe_ignore) r->fe_ignore = 0;
            else              r->fe_trigger = 1;
        }
        break;
    default:
        break;
    }
    return data;
}

static void ref_write(ref_t *r, vcsmap_kind_t k, uint16_t a, uint8_t d, int commit)
{
    /* Every mapper's write handlers sit on the same addresses as its read
     * taps, because real hardware cannot tell the two apart. */
    (void)ref_read(r, k, a, d, commit);
    if (!commit || !(a & 0x1000))
        return;
    switch (k) {
    case VCSMAP_CV: if (a >= 0x1400 && a <= 0x17FF) r->ram[a - 0x1400] = d; break;
    case VCSMAP_FA: if (a <= 0x10FF)                r->ram[a - 0x1000] = d; break;
    case VCSMAP_F8: case VCSMAP_F6: case VCSMAP_F4:
        if (r->superchip && a <= 0x107F)            r->ram[a - 0x1000] = d;
        break;
    default: break;
    }
}

/* -------------------------------------------------------------------------- */

static const char *kindname(vcsmap_kind_t k)
{
    switch (k) {
    case VCSMAP_FLAT: return "FLAT";
    case VCSMAP_F8:   return "F8";
    case VCSMAP_F6:   return "F6";
    case VCSMAP_F4:   return "F4";
    case VCSMAP_FA:   return "FA";
    case VCSMAP_E0:   return "E0";
    case VCSMAP_UA:   return "UA";
    case VCSMAP_FE:   return "FE";
    case VCSMAP_CV:   return "CV";
    default:          return "?";
    }
}

static uint32_t rng = 12345;
static uint32_t next_rand(void)
{
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static void run(vcsmap_kind_t k, uint32_t size, int superchip, unsigned steps)
{
    static uint8_t rom[64 * 1024];
    vcsmap_t m;
    ref_t r;
    unsigned i;

    /* Every byte distinct within its bank, so a wrong bank or a wrong slice
     * shows up immediately rather than aliasing onto the right answer. */
    for (i = 0; i < size; i++)
        rom[i] = (uint8_t)(i * 7u + (i >> 8) * 31u + 1u);

    vcsmap_init(&m, k, rom, size, superchip != 0);
    memset(&r, 0, sizeof r);
    r.rom = rom;
    r.rom_size = size;
    r.superchip = superchip;
    r.fe_ignore = 1;

    for (i = 0; i < steps; i++) {
        uint32_t x = next_rand();
        uint8_t bus = (uint8_t)next_rand();
        uint16_t a;
        int is_write = (x & 0x30000u) == 0;      /* a minority of accesses */

        /* Bias hard toward the hotspot pages: uniform random over 8K would
         * hit a four-address window about once in a thousand, which is not a
         * test of anything. */
        switch ((x >> 20) & 3u) {
        case 0:  a = (uint16_t)(0x1FE0u + (x & 0x1Fu)); break;
        case 1:  a = (uint16_t)(0x01E0u + (x & 0x3Fu)); break;
        case 2:  a = (uint16_t)(0x1000u + (x & 0x1FFu)); break;
        default: a = (uint16_t)(x & 0x1FFFu); break;
        }

        int got, want;
        if (is_write) {
            vcsmap_write(&m, a, bus, true);
            ref_write(&r, k, a, bus, 1);
            got = want = 0;
        } else {
            got  = vcsmap_serve(&m, a, bus, true);
            want = ref_read(&r, k, a, bus, 1);
        }

        if (got != want) {
            fprintf(stderr, "FAIL: %s size %u sc=%d step %u %s $%04X: "
                            "want %d got %d\n",
                    kindname(k), size, superchip, i,
                    is_write ? "write" : "read", a, want, got);
            fails++;
            return;
        }
        if ((int)m.bank != r.entry || m.slot[0] != r.e0[0]
            || m.slot[1] != r.e0[1] || m.slot[2] != r.e0[2]) {
            fprintf(stderr, "FAIL: %s step %u $%04X: bank state diverged "
                            "(bank %u/%d slots %u,%u,%u / %d,%d,%d)\n",
                    kindname(k), i, a, m.bank, r.entry,
                    m.slot[0], m.slot[1], m.slot[2],
                    r.e0[0], r.e0[1], r.e0[2]);
            fails++;
            return;
        }
        if (memcmp(m.ram, r.ram, sizeof m.ram) != 0) {
            fprintf(stderr, "FAIL: %s step %u $%04X: cartridge RAM diverged\n",
                    kindname(k), i, a);
            fails++;
            return;
        }
    }
}

/* The one thing the fuzz cannot state for itself: that a hotspot read returns
 * the OLD bank's byte. Written out longhand because it is the bug this whole
 * file exists to catch. */
static void test_hotspot_ordering(void)
{
    static uint8_t rom[8192];
    vcsmap_t m;
    unsigned i;

    for (i = 0; i < sizeof rom; i++)
        rom[i] = (uint8_t)(i >> 12);        /* every byte = its bank number */

    vcsmap_init(&m, VCSMAP_F8, rom, sizeof rom, false);

    if (vcsmap_serve(&m, 0x1FF9, 0, true) != 0) {
        fprintf(stderr, "FAIL: the hotspot read returned the NEW bank; MAME's "
                        "read tap runs after the read, so it must return the "
                        "old one\n");
        fails++;
    }
    if (m.bank != 1) {
        fprintf(stderr, "FAIL: the hotspot did not switch the bank\n");
        fails++;
    }
    if (vcsmap_serve(&m, 0x1000, 0, true) != 1) {
        fprintf(stderr, "FAIL: the switch did not apply to the next access\n");
        fails++;
    }
    /* And with commit off -- a debugger peek -- nothing moves. */
    vcsmap_serve(&m, 0x1FF8, 0, false);
    if (m.bank != 1) {
        fprintf(stderr, "FAIL: a non-committing access moved the bank; the "
                        "MAME debugger would shift it under a running game\n");
        fails++;
    }
}

/* The .cfg sibling. Size cannot tell an 8K F8 from an 8K E0, UA or FE -- they
 * are all 8192 bytes -- so a one-line file names the board. What matters here
 * is that it is read tolerantly (case, blanks, trailing junk), that "F8SC" is
 * not eaten by the "F8" entry, and that an unrecognised name yields NONE so
 * the caller falls back to size rather than refusing to boot. */
static void test_cfg_hint(void)
{
    static const struct { const char *s; vcsmap_kind_t k; bool sc; } cases[] = {
        { "F8\n",            VCSMAP_F8,   false },
        { "f8",              VCSMAP_F8,   false },
        { "  E0\r\n",        VCSMAP_E0,   false },
        { "UA # up to date", VCSMAP_UA,   false },
        { "FE",              VCSMAP_FE,   false },
        { "F8SC\n",          VCSMAP_F8,   true  },
        { "f6sc",            VCSMAP_F6,   true  },
        { "F4SC",            VCSMAP_F4,   true  },
        { "FA",              VCSMAP_FA,   false },
        { "CV\n",            VCSMAP_CV,   false },
        { "4K",              VCSMAP_FLAT, false },
        { "3E",              VCSMAP_NONE, false },   /* not implemented yet */
        { "",                VCSMAP_NONE, false },
        { "   ",             VCSMAP_NONE, false },
    };
    unsigned i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        unsigned len = (unsigned)strlen(cases[i].s);
        vcsmap_kind_t k = vcsmap_from_name(cases[i].s, len);
        bool sc = (k != VCSMAP_NONE) && vcsmap_name_is_sc(cases[i].s, len);

        if (k != cases[i].k) {
            fprintf(stderr, "FAIL: .cfg \"%s\" read as %s, want %s\n",
                    cases[i].s, vcsmap_name(k), vcsmap_name(cases[i].k));
            fails++;
        }
        if (sc != cases[i].sc) {
            fprintf(stderr, "FAIL: .cfg \"%s\" superchip %d, want %d\n",
                    cases[i].s, (int)sc, (int)cases[i].sc);
            fails++;
        }
    }
}

/* The hint is spent by the image it names, and by no other. MediaTypeROM
 * pushes a .cfg only when the file exists, so a mount with no sibling sends
 * nothing at all -- a hint left standing would be applied to the NEXT game. */
static void test_hint_is_spent(void)
{
    static uint8_t e0img[8192], f8img[8192];
    vcs_mem_t m;
    unsigned i;

    for (i = 0; i < sizeof e0img; i++) {
        e0img[i] = (uint8_t)(i * 7 + 1);
        f8img[i] = (uint8_t)(i * 11 + 3);
    }
    memset(&m, 0, sizeof m);

    vcs_set_cfg(&m, "E0\n", 3);
    vcs_set_image(&m, e0img, sizeof e0img);
    if (m.map.kind != VCSMAP_E0) {
        fprintf(stderr, "FAIL: the .cfg hint did not reach the mapper "
                        "(got %s)\n", vcsmap_name(m.map.kind));
        fails++;
    }

    /* Second mount, no sibling. Size detection must decide, not the ghost. */
    vcs_set_image(&m, f8img, sizeof f8img);
    if (m.map.kind != VCSMAP_F8) {
        fprintf(stderr, "FAIL: a stale .cfg hint survived into the next "
                        "image (served %s, want F8)\n",
                vcsmap_name(m.map.kind));
        fails++;
    }
}

/* Two boards switch on addresses BELOW A12, where the cartridge is not
 * selected at all and can only watch: UA on $0200-$027F, FE on $01FE. Both the
 * RP2040 bus loop and the MAME model skip A12-low cycles by default -- that is
 * the right default, it is most of them -- so vcsmap flags the two boards that
 * need looking at, and everything else must stay unflagged or the cartridge
 * pays for a data sample on every zero-page access a game makes. */
static void test_watch_low(void)
{
    static const struct { vcsmap_kind_t k; uint32_t len; bool watch; } cases[] = {
        { VCSMAP_FLAT, 4096,  false }, { VCSMAP_F8, 8192,  false },
        { VCSMAP_F6,  16384,  false }, { VCSMAP_F4, 32768, false },
        { VCSMAP_FA,  12288,  false }, { VCSMAP_E0,  8192, false },
        { VCSMAP_CV,   2048,  false },
        { VCSMAP_UA,   8192,  true  }, { VCSMAP_FE,  8192, true  },
    };
    static uint8_t img[32768];
    vcs_mem_t m;
    unsigned i;

    for (i = 0; i < sizeof img; i++)
        img[i] = (uint8_t)(i * 7 + 1);

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        vcsmap_t map;

        vcsmap_init(&map, cases[i].k, img, cases[i].len, false);
        if (map.watch_low != cases[i].watch) {
            fprintf(stderr, "FAIL: %s watch_low %d, want %d\n",
                    vcsmap_name(cases[i].k), (int)map.watch_low,
                    (int)cases[i].watch);
            fails++;
        }
    }

    /* And the gate does what it says: a UA game banks from $0240, an F8 game
     * mapped at the same moment does not notice the cycle at all. */
    memset(&m, 0, sizeof m);
    vcs_set_cfg(&m, "UA", 2);
    vcs_set_image(&m, img, 8192);
    vcs_watch(&m, 0x0240, 0x00);
    if (m.map.bank != 1) {
        fprintf(stderr, "FAIL: UA did not bank from $0240 -- the low bus is "
                        "not reaching the mapper\n");
        fails++;
    }
    vcs_watch(&m, 0x0200, 0x00);
    if (m.map.bank != 0) {
        fprintf(stderr, "FAIL: UA did not bank back from $0200\n");
        fails++;
    }

    memset(&m, 0, sizeof m);
    vcs_set_image(&m, img, 8192);           /* no .cfg: an 8K F8 by size */
    vcs_watch(&m, 0x0240, 0x00);
    if (m.map.kind != VCSMAP_F8 || m.map.bank != 0) {
        fprintf(stderr, "FAIL: an F8 game reacted to a $0240 cycle\n");
        fails++;
    }
}

int main(void)
{
    const unsigned N = 200000;

    test_hotspot_ordering();
    test_cfg_hint();
    test_hint_is_spent();
    test_watch_low();

    run(VCSMAP_FLAT, 2048, 0, N);
    run(VCSMAP_FLAT, 4096, 0, N);
    run(VCSMAP_F8,   8192, 0, N);
    run(VCSMAP_F8,   8192, 1, N);       /* F8SC */
    run(VCSMAP_F6,  16384, 0, N);
    run(VCSMAP_F6,  16384, 1, N);
    run(VCSMAP_F4,  32768, 0, N);
    run(VCSMAP_F4,  32768, 1, N);
    run(VCSMAP_FA,  12288, 0, N);
    run(VCSMAP_E0,   8192, 0, N);
    run(VCSMAP_UA,   8192, 0, N);
    run(VCSMAP_FE,   8192, 0, N);
    run(VCSMAP_CV,   2048, 0, N);

    if (fails) {
        printf("test_vcsmap: %d failures\n", fails);
        return 1;
    }
    printf("test_vcsmap: PASS (9 schemes, 13 configurations, %u fuzzed "
           "accesses each, byte and bank state against MAME's handlers, "
           "plus the .cfg hint, its one-shot lifetime, and the low-bus watch)\n", N);
    return 0;
}
