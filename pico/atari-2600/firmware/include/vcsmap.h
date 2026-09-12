/* vcsmap.h -- the real Atari 2600 cartridge mappers.
 *
 * When a client boots a GAME, the cartridge has to behave like whatever board
 * that game shipped on. This is that behaviour, and like vcs_cart.h it lives
 * in the header as static inline so core1, the MAME device and the host tests
 * compile ONE implementation.
 *
 * VERIFIED AGAINST MAME'S OWN HANDLERS. host_test/test_vcsmap.c carries
 * verbatim transcriptions of src/devices/bus/vcs/rom.cpp and requires a fuzzed
 * access walk to agree byte for byte, bank state included. That is the
 * ColecoVision port's discipline: an expectation written by calling the code
 * under test proves only that the code agrees with itself.
 *
 * SCOPE, and why it stops where it does. Only schemes MAME implements are
 * here, because a scheme with no reference cannot be verified against
 * anything -- transcribing PlusCart's version and then testing it against
 * itself would be theatre. That rules out F0, EF, DF, BF and SB. Also out:
 * ACE and ELF, which are ARM-code cartridges (PlusCart relocates and RUNS
 * Thumb on the cartridge MCU); DPC and the Supercharger, which carry their own
 * coprocessor or BIOS; and 3E/3F, which need the cart to sample the data bus
 * on an access BELOW $1000 -- doable with the same technique the mailbox uses,
 * but it puts the timing-critical sampling in the serve path of every booted
 * game rather than only in the mailbox.
 *
 * THE `commit` PARAMETER is the same idea as the ColecoVision's speculative
 * serve. Every classic 2600 mapper switches banks on a READ -- there is no R/W
 * line, so the hotspot is the address -- which means a debugger peek would
 * move the bank under the running game. MAME's device passes
 * !side_effects_disabled() and core1 passes true.
 */

#ifndef VCSMAP_H
#define VCSMAP_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    VCSMAP_NONE = 0,
    VCSMAP_FUJI,        /* our own: banked low 2K, fixed high 2K            */
    VCSMAP_FLAT,        /* 2K (mirrored into 4K) or plain 4K                */
    VCSMAP_F8,          /* 8K, 2 banks,  hotspots $1FF8-$1FF9               */
    VCSMAP_F6,          /* 16K, 4 banks, $1FF6-$1FF9                        */
    VCSMAP_F4,          /* 32K, 8 banks, $1FF4-$1FFB                        */
    VCSMAP_FA,          /* 12K, 3 banks, $1FF8-$1FFA, + 256 bytes of RAM    */
    VCSMAP_E0,          /* 8K in 1K slices; the top slice is fixed          */
    VCSMAP_UA,          /* 8K, 2 banks, switched from $0200-$027F           */
    VCSMAP_FE,          /* 8K, 2 banks, chosen by D5 of the NEXT access     */
    VCSMAP_CV,          /* 2K + 1K of RAM                                   */
    VCSMAP_NKINDS
} vcsmap_kind_t;

#define VCSMAP_RAM_MAX 1024u

typedef struct {
    vcsmap_kind_t kind;
    const uint8_t *rom;
    uint32_t rom_size;

    uint8_t bank;               /* F8/F6/F4/FA/UA/FE */
    uint8_t slot[3];            /* E0 */
    bool superchip;             /* the 128-byte Super Chip is fitted */
    bool watch_low;             /* the board switches on addresses below $1000 */

    /* FE watches for an access to $01FE and then takes the bank from bit 5 of
     * whatever the NEXT access puts on the bus. The first read after reset is
     * ignored, exactly as MAME does it. */
    bool fe_trigger;
    bool fe_ignore_first;

    uint8_t ram[VCSMAP_RAM_MAX];
} vcsmap_t;

/* Which sizes each scheme accepts. MAME's own cartridge whitelist is stricter
 * still; tools/checkrom.py enforces ours. */
static inline uint32_t vcsmap_banks(vcsmap_kind_t k)
{
    switch (k) {
    case VCSMAP_F8: case VCSMAP_UA: case VCSMAP_FE: return 2;
    case VCSMAP_F6: return 4;
    case VCSMAP_F4: return 8;
    case VCSMAP_FA: return 3;
    case VCSMAP_E0: return 8;       /* 1K slices, not 4K banks */
    default:        return 1;
    }
}

static inline void vcsmap_reset(vcsmap_t *m)
{
    m->bank = 0;
    m->slot[0] = m->slot[1] = m->slot[2] = 0;
    m->fe_trigger = false;
    m->fe_ignore_first = true;
    memset(m->ram, 0, sizeof m->ram);
}

static inline void vcsmap_init(vcsmap_t *m, vcsmap_kind_t k,
                               const uint8_t *rom, uint32_t len, bool superchip)
{
    m->kind = k;
    m->rom = rom;
    m->rom_size = len;
    m->superchip = superchip;
    /* UA switches on $0200-$027F and FE on $01FE/$01FF -- below A12, so those
     * cycles never select the cartridge and the board is watching the bus
     * rather than answering it. Both the RP2040 loop and the MAME model skip
     * A12-low addresses by default, so each has to be told to look. */
    m->watch_low = (k == VCSMAP_UA || k == VCSMAP_FE);
    vcsmap_reset(m);
}

/* A byte of ROM, with the 2K mirror the small carts rely on. */
static inline uint8_t vcsmap_rom(const vcsmap_t *m, uint32_t off)
{
    if (!m->rom || m->rom_size == 0)
        return 0xFFu;
    return m->rom[off % m->rom_size];
}

/* One access. Returns the byte the cartridge drives, or -1 if it drives
 * nothing (below $1000). `data` is what is on the bus, which only FE needs.
 * Side effects happen only when `commit`.
 *
 * THE ORDER IS THE WHOLE POINT, and it is easy to get backwards. MAME
 * installs a read BANK and a read TAP over the same range, and a tap runs
 * AFTER the read it is attached to -- emumem_het.cpp is unambiguous:
 *
 *     uX data = this->m_next->read(offset, mem_mask);
 *     m_tap(offset, data, mem_mask);
 *
 * So an access to a bankswitch hotspot returns the byte from the bank that
 * was live BEFORE it, and the switch applies to the next access. Getting this
 * inverted is the classic mapper bug -- it is the same distinction the
 * ColecoVision port found between MegaCart and X-in-1 -- and it is invisible
 * until a game reads its hotspot for data as well as for the side effect,
 * which several do.
 *
 * Everything below therefore computes the returned byte from the CURRENT
 * state first, and only then applies side effects.
 */
static inline int vcsmap_serve(vcsmap_t *m, uint16_t addr, uint8_t data,
                               bool commit)
{
    uint16_t a = (uint16_t)(addr & 0x1FFFu);
    int out = -1;

    /* ---- 1. what the cartridge drives, from the state as it stands ---- */
    if (a & 0x1000u) {
        switch (m->kind) {
        case VCSMAP_FUJI:
            return -1;              /* served by vcs_cart.h, not here */

        case VCSMAP_FLAT:
            out = vcsmap_rom(m, (uint32_t)(a & 0x0FFFu));
            break;

        case VCSMAP_CV:
            /* 2K mirrored across the window with 1K of RAM read at
             * $1000-$13FF. The RAM read handler covers only that range, NOT
             * its mirror at $1800-$1BFF, which still reads ROM. Transcribed
             * rather than tidied: install_rom(..., 0x800 mirror) followed by
             * install_read_handler over part of it is exactly what MAME does. */
            if (a <= 0x13FFu)
                out = m->ram[a - 0x1000u];
            else
                out = vcsmap_rom(m, (uint32_t)(a & 0x07FFu));
            break;

        case VCSMAP_F8:
        case VCSMAP_F6:
        case VCSMAP_F4:
        case VCSMAP_FA:
            /* The Super Chip, when fitted: 128 bytes read at $1080-$10FF and
             * written at $1000-$107F. FA's is 256 bytes at $1100 / $1000. */
            if (m->kind == VCSMAP_FA && a >= 0x1100u && a <= 0x11FFu)
                out = m->ram[a - 0x1100u];
            else if (m->kind != VCSMAP_FA && m->superchip
                     && a >= 0x1080u && a <= 0x10FFu)
                out = m->ram[a - 0x1080u];
            else
                out = vcsmap_rom(m, (uint32_t)m->bank * 0x1000u + (a & 0x0FFFu));
            break;

        case VCSMAP_E0:
            /* Three 1K slots plus a fixed one. The top slice is hardwired to
             * slice 7, which is what makes a console RESET survivable: $1FFC
             * is always in it. */
            if (a < 0x1400u)
                out = vcsmap_rom(m, (uint32_t)m->slot[0] * 0x400u + (a & 0x3FFu));
            else if (a < 0x1800u)
                out = vcsmap_rom(m, (uint32_t)m->slot[1] * 0x400u + (a & 0x3FFu));
            else if (a < 0x1C00u)
                out = vcsmap_rom(m, (uint32_t)m->slot[2] * 0x400u + (a & 0x3FFu));
            else
                out = vcsmap_rom(m, 7u * 0x400u + (a & 0x3FFu));
            break;

        case VCSMAP_UA:
        case VCSMAP_FE:
            out = vcsmap_rom(m, (uint32_t)m->bank * 0x1000u + (a & 0x0FFFu));
            break;

        default:
            return -1;
        }
    }

    if (!commit)
        return out;

    /* ---- 2. side effects, after the read, exactly as MAME's taps run ---- */
    switch (m->kind) {
    case VCSMAP_F8:
        if (a >= 0x1FF8u && a <= 0x1FF9u) m->bank = (uint8_t)(a - 0x1FF8u);
        break;
    case VCSMAP_F6:
        if (a >= 0x1FF6u && a <= 0x1FF9u) m->bank = (uint8_t)(a - 0x1FF6u);
        break;
    case VCSMAP_F4:
        if (a >= 0x1FF4u && a <= 0x1FFBu) m->bank = (uint8_t)(a - 0x1FF4u);
        break;
    case VCSMAP_FA:
        if (a >= 0x1FF8u && a <= 0x1FFAu) m->bank = (uint8_t)(a - 0x1FF8u);
        break;
    case VCSMAP_E0:
        if (a >= 0x1FE0u && a <= 0x1FE7u)      m->slot[0] = (uint8_t)(a & 7u);
        else if (a >= 0x1FE8u && a <= 0x1FEFu) m->slot[1] = (uint8_t)(a & 7u);
        else if (a >= 0x1FF0u && a <= 0x1FF7u) m->slot[2] = (uint8_t)(a & 7u);
        break;
    case VCSMAP_UA:
        if (a >= 0x0200u && a <= 0x027Fu)
            m->bank = (uint8_t)((a >> 6) & 1u);
        break;
    case VCSMAP_FE:
        /* An access to $01FE arms it; the bank then comes from bit 5 of
         * whatever the NEXT access put on the bus -- which for a cartridge
         * read is the byte just returned. The first read after reset is
         * ignored, as MAME does. */
        if (m->fe_trigger && (a == 0x01FFu || (a & 0x1000u))) {
            uint8_t d = (out >= 0) ? (uint8_t)out : data;
            m->bank = (uint8_t)((d & 0x20u) ? 0u : 1u);
            m->fe_trigger = false;
        }
        if (a == 0x01FEu) {
            if (m->fe_ignore_first)
                m->fe_ignore_first = false;
            else
                m->fe_trigger = true;
        }
        break;
    default:
        break;
    }
    return out;
}

/* A store into cartridge space. Real hardware cannot tell this from a read --
 * which is why every classic mapper decodes the ADDRESS -- so the hotspots
 * fire either way. What this adds is the RAM write halves. */
static inline void vcsmap_write(vcsmap_t *m, uint16_t addr, uint8_t data,
                                bool commit)
{
    uint16_t a = (uint16_t)(addr & 0x1FFFu);

    (void)vcsmap_serve(m, a, data, commit);   /* the hotspots, identically */

    if (!(a & 0x1000u) || !commit)
        return;

    switch (m->kind) {
    case VCSMAP_CV:
        if (a >= 0x1400u && a <= 0x17FFu)
            m->ram[a - 0x1400u] = data;
        break;
    case VCSMAP_FA:
        if (a <= 0x10FFu)
            m->ram[a - 0x1000u] = data;
        break;
    case VCSMAP_F8:
    case VCSMAP_F6:
    case VCSMAP_F4:
        if (m->superchip && a <= 0x107Fu)
            m->ram[a - 0x1000u] = data;
        break;
    default:
        break;
    }
}

/* Which board an image shipped on, from its size.
 *
 * Size alone is how MAME's own identify_cart_type() starts, and for the sizes
 * that have one dominant scheme it is all you need. The ambiguous ones -- 8K
 * is F8 far more often than E0, UA or FE -- are where the .cfg sibling of the
 * DBC push comes in: the ColecoVision port reads its .cfg for exactly this,
 * and this is where that hook goes when M6's corpus turns up a cartridge the
 * default gets wrong.
 */
static inline vcsmap_kind_t vcsmap_detect(const uint8_t *rom, uint32_t len)
{
    (void)rom;
    switch (len) {
    case 2048:  return VCSMAP_FLAT;
    case 4096:  return VCSMAP_FLAT;
    case 8192:  return VCSMAP_F8;
    case 12288: return VCSMAP_FA;
    case 16384: return VCSMAP_F6;
    case 32768: return VCSMAP_F4;
    default:    return VCSMAP_FLAT;
    }
}

/* MAME's detect_super_chip() heuristic, in the shape it uses: a Super Chip
 * cartridge leaves its first 256 bytes -- the RAM window -- as a repeated
 * pattern rather than code. */
static inline bool vcsmap_has_superchip(const uint8_t *rom, uint32_t len)
{
    unsigned i;

    if (!rom || (len != 8192 && len != 16384 && len != 32768))
        return false;
    for (i = 0; i < 256; i++)
        if (rom[i] != rom[0])
            return false;
    return true;
}

/* Parse a scheme name out of the .cfg sibling of a pushed image.
 *
 * Size alone cannot tell an 8K F8 from an 8K E0, UA or FE -- they are all
 * 8192 bytes and only the board differs -- so something has to say which, and
 * the DBC push already carries a second stream for exactly this. The
 * ColecoVision port reads its .cfg the same way; every other port in the
 * family accepts it and throws it away.
 *
 * Tolerant on purpose: leading blanks, any case, and anything after the name
 * (a comment, a newline, a whole ini file) is ignored. An unrecognised name
 * falls back to size detection rather than refusing to boot. */
static inline vcsmap_kind_t vcsmap_from_name(const char *s, unsigned len)
{
    static const struct { const char *n; unsigned l; vcsmap_kind_t k; } tab[] = {
        { "FLAT", 4, VCSMAP_FLAT }, { "2K", 2, VCSMAP_FLAT },
        { "4K", 2, VCSMAP_FLAT },
        { "F8SC", 4, VCSMAP_F8 },   { "F8", 2, VCSMAP_F8 },
        { "F6SC", 4, VCSMAP_F6 },   { "F6", 2, VCSMAP_F6 },
        { "F4SC", 4, VCSMAP_F4 },   { "F4", 2, VCSMAP_F4 },
        { "FA", 2, VCSMAP_FA },     { "E0", 2, VCSMAP_E0 },
        { "UA", 2, VCSMAP_UA },     { "FE", 2, VCSMAP_FE },
        { "CV", 2, VCSMAP_CV },
    };
    unsigned i, j, start = 0;

    while (start < len && (s[start] == ' ' || s[start] == '\t'
                           || s[start] == '\r' || s[start] == '\n'))
        start++;

    /* Longest names first, so "F8SC" is not eaten by "F8". */
    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        if (start + tab[i].l > len)
            continue;
        for (j = 0; j < tab[i].l; j++) {
            char c = s[start + j];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            if (c != tab[i].n[j])
                break;
        }
        if (j == tab[i].l)
            return tab[i].k;
    }
    return VCSMAP_NONE;         /* caller falls back to size detection */
}

/* Does the .cfg name a Super Chip variant? */
static inline bool vcsmap_name_is_sc(const char *s, unsigned len)
{
    unsigned i;
    for (i = 0; i + 1 < len; i++) {
        char a = s[i], b = s[i + 1];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a == 'S' && b == 'C')
            return true;
    }
    return false;
}

static inline const char *vcsmap_name(vcsmap_kind_t k)
{
    switch (k) {
    case VCSMAP_FUJI: return "FUJI";
    case VCSMAP_FLAT: return "FLAT";
    case VCSMAP_F8:   return "F8";
    case VCSMAP_F6:   return "F6";
    case VCSMAP_F4:   return "F4";
    case VCSMAP_FA:   return "FA";
    case VCSMAP_E0:   return "E0";
    case VCSMAP_UA:   return "UA";
    case VCSMAP_FE:   return "FE";
    case VCSMAP_CV:   return "CV";
    default:          return "none";
    }
}

#endif /* VCSMAP_H */
