/* vcsmap.h -- the 2600 cartridge boards a booted game can need. Static inline so core1,
 * the MAME device and the host tests compile one implementation; test_vcsmap.c checks it
 * against MAME's own handlers. Only schemes MAME implements are here, since nothing else
 * could be verified; 3E/3F are out because they would put data-bus sampling below $1000
 * in every game's serve path.
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

    /* FE: an access to $01FE arms it; bit 5 of the next access picks the bank. */
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
    /* UA and FE switch on addresses below A12, which never select the cart; the bus
     * loop and the MAME model skip those unless told to watch. */
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

/* One access: the byte the cart drives, or -1 below $1000. `data` is the bus, for FE.
 * Every classic mapper switches on a read, so `commit` keeps a debugger peek from moving
 * the bank: MAME passes !side_effects_disabled(), core1 true. A hotspot returns the byte
 * from the bank live before it, as MAME's taps run after the read (emumem_het.cpp);
 * several games read their hotspot for data.
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
            /* RAM reads at $1000-$13FF only; its mirror at $1800 still reads ROM,
             * as MAME installs it. */
            if (a <= 0x13FFu)
                out = m->ram[a - 0x1000u];
            else
                out = vcsmap_rom(m, (uint32_t)(a & 0x07FFu));
            break;

        case VCSMAP_F8:
        case VCSMAP_F6:
        case VCSMAP_F4:
        case VCSMAP_FA:
            /* Super Chip: read $1080-$10FF, write $1000-$107F. FA's 256 bytes: $1100/$1000. */
            if (m->kind == VCSMAP_FA && a >= 0x1100u && a <= 0x11FFu)
                out = m->ram[a - 0x1100u];
            else if (m->kind != VCSMAP_FA && m->superchip
                     && a >= 0x1080u && a <= 0x10FFu)
                out = m->ram[a - 0x1080u];
            else
                out = vcsmap_rom(m, (uint32_t)m->bank * 0x1000u + (a & 0x0FFFu));
            break;

        case VCSMAP_E0:
            /* The top slot is always slice 7, so a console RESET finds $1FFC. */
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

    /* ---- 2. side effects, after the read, as MAME's taps run ---- */
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
        /* For a cart read the next access's byte is the one just returned. The
         * first $01FE after reset is ignored, as MAME does. */
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

/* A store: the hotspots fire as for a read (there is no R/W line), plus the RAM writes. */
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

/* The board from size alone, as MAME's identify_cart_type() starts. 8K is usually F8;
 * the .cfg sibling names E0, UA or FE. */
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

/* MAME's detect_super_chip(): a Super Chip image fills its RAM window, the first 256
 * bytes, with one repeated byte. */
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

/* The scheme a pushed image's .cfg sibling names. Leading blanks, any case, and anything
 * after the name are accepted; an unknown name falls back to size detection rather than
 * refusing to boot. */
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
