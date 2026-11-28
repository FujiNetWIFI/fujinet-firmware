/* vcs_cart.h -- the 6507 bus as seen from inside an Atari 2600 cartridge, static inline so
 * core1, the MAME device and the host tests compile one implementation.
 *
 * The edge carries A0-A12, D0-D7, +5V and GND: no R/W, clock or reset. A read of a write port
 * looks exactly like a write, so registers are armed then committed, and a written byte is
 * recovered from the data bus (vcs_recover()), which is why the write pages are never driven.
 */
#ifndef VCS_CART_H
#define VCS_CART_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fuji_mailbox.h"

/* VCS_CART_HOST_MAPPER: the host emulator (Stella) serves a booted game with its own cartridge
 * classes, so the decode below keeps only the FujiNet layout. */
#ifdef VCS_CART_HOST_MAPPER
/* Longest scheme name a .cfg sibling can carry, NUL included. */
#define VCS_CART_CFG_MAX 16
#else
#include "vcsmap.h"
#endif

/* What a store meant, once decoded. */
typedef enum {
    VCS_EV_NONE = 0,
    VCS_EV_REG,      /* a completed register write: ev_a = register, ev_b = value */
    VCS_EV_TX,       /* append ev_b to the TX stream                              */
    VCS_EV_BANK,     /* map bank ev_b into the low 2K                             */
    VCS_EV_SWAP,     /* serve the staged image                                    */
    VCS_EV_TROW,     /* begin text row ev_b                                       */
    VCS_EV_TCHR,     /* append character ev_b to the row being composed           */
    VCS_EV_TEND,     /* render the composed row, attribute ev_b                   */
    VCS_EV_BLIT,     /* run the staged blit, transform ev_b                       */
    VCS_EV_ARMED,    /* the decode gate just opened                               */
    VCS_EV_PATHTX,   /* emit the path buffer into the TX stream, padded to 256     */
    VCS_EV_PATHRAW,  /* emit just the path buffer's own bytes                      */
} vcs_ev_t;

/* A path op too long to run between a store and the next fetch; see path_job. */
typedef enum {
    VCS_JOB_NONE = 0,
    VCS_JOB_COPY,    /* job_src over job_dst, job_len bytes */
    VCS_JOB_POP,     /* walk job_dst back to its last '/'   */
} vcs_job_t;

/* Work per step, small enough that core1 still sees the address move on in time. */
#define VCS_JOB_COPY_STEP 8u    /* bytes; FN_PATH_MAX is a multiple of it */
#define VCS_JOB_POP_STEP  2u    /* characters examined                    */

typedef struct {
    /* The 4K the console sees, $1000-$1FFF; the cart paints the reply, status page and text
     * planes into it. Only the fixed half is served from here: the low half goes through
     * `lowbank`, because a bank switch must finish before the next fetch, 838 ns away. */
    uint8_t win[FN_WINDOW_SIZE];

    /* The 2K currently mapped at $1000. Points into the image. */
    const uint8_t *lowbank;

    /* A booted game is served by its own board's mapper; its mailbox is dead by then. */
#ifndef VCS_CART_HOST_MAPPER
    vcsmap_t map;
#endif

    /* The Battleship board being composed: FN_BLIT_HULLS overlays what FN_BLIT_FIELD left,
     * and the planes cannot be read back as characters. */
    uint8_t board[FN_BOARD_CELLS];

    /* The four path buffers, selected by FN_PATH_SEL0..SEL3; see fuji_mailbox.h. */
    uint8_t path[FN_PATH_BUFS][FN_PATH_MAX];
    uint16_t path_len[FN_PATH_BUFS];
    uint8_t path_sel;

    /* COMMIT, SEED and POP walk a buffer, which takes longer than core1 has before the next
     * fetch. With VCS_CART_DEFER_PATH the walk waits here and vcs_path_step() runs it a piece
     * at a time while core1 holds a read; any later path op but a select, and any blit,
     * finishes it first. */
    uint8_t path_job;         /* vcs_job_t                                      */
    uint8_t job_src, job_dst; /* buffers; a pop has only a destination          */
    uint16_t job_pos, job_len;

    /* A scheme named by the pushed image's .cfg sibling, if it had one. */
#ifdef VCS_CART_HOST_MAPPER
    /* The name verbatim, for the host to map onto its own scheme enum.
     * Spent by the next vcs_set_image() exactly as the typed hint is. */
    char cfg_name[VCS_CART_CFG_MAX];
#else
    vcsmap_kind_t hint;
    bool hint_sc;
#endif

    const uint8_t *image;     /* client or booted game                          */
    uint32_t image_size;
    uint8_t bank;             /* which 2K of the image is mapped low            */

    bool mailbox;             /* the served image claimed the mailbox           */
    bool armed;               /* the decode gate (see FN_HOT_ARM1/ARM2)         */
    uint8_t arm_step;         /* how far through the two-store arming sequence  */
    bool swap_armed;          /* FN_REG_BOOTLOCK was written                    */

    /* The armed register: a lone stray arm carries no value and a lone commit has nothing
     * armed, so one stray mutates nothing. Spent after one use, as in fujimail.c. */
    uint8_t regsel;
    bool regsel_armed;

    /* Staged blit parameters, latched by the FN_HOT_BLIT_* stores. */
    uint16_t blit_src, blit_dst;
    uint8_t blit_cnt;

    /* The text row being composed between FN_HOT_TROW and FN_HOT_TEND, here so the cart and
     * the MAME device hand vcs_render_row() the same three fields. */
    uint8_t trow;
    uint8_t tbuf[FN_T_COLS];
    uint8_t tlen;
} vcs_mem_t;

/* The largest image that can be served: MAME's cartridge whitelist and the
 * (N+1)*2048 client layout meet at 32K. */
#define VCS_IMAGE_MAX 32768u

/* A client is (N+1) * 2048 bytes, but a game can be any whole number of 2K blocks and only the
 * claim tells them apart, once the bytes are in; so accept those and let vcs_set_image()
 * decide. Called at OPEN, before the ESP32 reads the file. */
static inline uint8_t vcs_gate(uint32_t size)
{
    if (size < FN_BANK_SIZE || (size % FN_BANK_SIZE) != 0u)
        return FN_BOOT_ERR_NOMAP;
    if (size > VCS_IMAGE_MAX)
        return FN_BOOT_ERR_TOOBIG;
    return 0;
}

/* Recover the byte a store put on the bus: the cart samples D0-D7 while the address is stable
 * and keeps the second-to-last sample, the last being taken as the bus turns over (PlusROM's
 * and Superchip's idiom). MAME hands its device a clean byte, so test_busio.c covers this. */
static inline uint8_t vcs_recover(const uint8_t *samples, unsigned n)
{
    if (n >= 2)
        return samples[n - 2];
    return n ? samples[0] : 0xFFu;
}

/* Console address -> window offset. A12 is the whole chip select. */
static inline bool vcs_selected(uint16_t a)
{
    return (a & 0x1000u) != 0;
}

static inline uint16_t vcs_off(uint16_t a)
{
    return (uint16_t)(a & 0x0FFFu);
}

/* The control and TX pages are never driven. On real hardware a read here
 * returns the floating bus; the client must never do it, and checkrom.py
 * fails any image that does. */
static inline bool vcs_tristate(uint16_t a)
{
    uint16_t p = (uint16_t)(a & 0x0F00u);
    return p == (FN_H_REGSEL & 0x0F00u) || p == (FN_TX_BASE & 0x0F00u);
}

/* `commit` gates the side effects a booted game's mapper has on a READ --
 * every classic 2600 scheme switches banks from the address, so a debugger
 * peek would move the bank under the running game. MAME's device passes
 * !side_effects_disabled(); core1 passes true. */
static inline uint8_t vcs_read_ex(const vcs_mem_t *m, uint16_t a, bool commit)
{
    if (!vcs_selected(a))
        return 0xFF;
#ifdef VCS_CART_HOST_MAPPER
    (void)commit;             /* no read here has a side effect; see below */
    if (vcs_tristate(a))
        return 0xFF;          /* open bus; nothing here is ours to drive */
#else
    if (m->map.kind == VCSMAP_FUJI && vcs_tristate(a))
        return 0xFF;          /* open bus; nothing here is ours to drive */

    if (m->map.kind != VCSMAP_FUJI) {
        int b = vcsmap_serve((vcsmap_t *)&m->map, a, 0xFFu, commit);
        return (b >= 0) ? (uint8_t)b : 0xFFu;
    }
#endif

    uint16_t off = vcs_off(a);
    if (off < FN_BANK_SIZE)
        return m->lowbank ? m->lowbank[off] : 0xFFu;
    return m->win[off];
}

static inline uint8_t vcs_read(const vcs_mem_t *m, uint16_t a)
{
    return vcs_read_ex(m, a, true);
}

/* Republish the path length, so a client knows how much of a 256-byte payload the cart
 * filled. */
static inline void vcs_publish_pathlen(vcs_mem_t *m)
{
    uint16_t n = m->path_len[m->path_sel];

    m->win[FN_B_PATHLEN - FN_WINDOW_BASE] = (uint8_t)(n & 0xFFu);
    m->win[FN_B_PATHLEN + 1 - FN_WINDOW_BASE] = (uint8_t)(n >> 8);
}

/* Byte i of the path payload: the buffer, then NUL padding to exactly 256, because
 * OPEN_DIRECTORY and SET_DEVICE_FULLPATH fail a short payload. */
static inline uint8_t vcs_path_byte(const vcs_mem_t *m, unsigned i)
{
    return (i < m->path_len[m->path_sel]) ? m->path[m->path_sel][i] : 0u;
}

/* One piece of the pending path job. */
static inline void vcs_path_step(vcs_mem_t *m)
{
    if (m->path_job == VCS_JOB_COPY) {
        /* Whole steps even past the end: nothing reads a buffer beyond its length. */
        memcpy(&m->path[m->job_dst][m->job_pos], &m->path[m->job_src][m->job_pos],
               VCS_JOB_COPY_STEP);
        m->job_pos = (uint16_t)(m->job_pos + VCS_JOB_COPY_STEP);
        if (m->job_pos >= m->job_len)
            m->path_job = VCS_JOB_NONE;
    } else if (m->path_job == VCS_JOB_POP) {
        const uint8_t *buf = m->path[m->job_dst];
        unsigned left = VCS_JOB_POP_STEP;

        while (left > 0u && m->job_pos > 1u && buf[m->job_pos - 1u] != (uint8_t)'/') {
            m->job_pos--;
            left--;
        }
        if (m->job_pos <= 1u || buf[m->job_pos - 1u] == (uint8_t)'/') {
            m->path_len[m->job_dst] = m->job_pos;
            m->path_job = VCS_JOB_NONE;
            vcs_publish_pathlen(m);
        }
    }
}

static inline void vcs_path_finish(vcs_mem_t *m)
{
    while (m->path_job != VCS_JOB_NONE)
        vcs_path_step(m);
}

/* Start copying buffer `src` over `dst`. The new length is published at once. */
static inline void vcs_path_copy(vcs_mem_t *m, uint8_t src, uint8_t dst)
{
    m->job_src = src;
    m->job_dst = dst;
    m->job_pos = 0;
    m->job_len = m->path_len[src];
    m->path_len[dst] = m->job_len;
    m->path_job = m->job_len ? VCS_JOB_COPY : VCS_JOB_NONE;
}

/* An access with A12 low: the cart does not answer it, but UA and FE boards watch it. One test
 * per low cycle, since this runs on every zero-page access a game makes. */
static inline void vcs_watch(vcs_mem_t *m, uint16_t a, uint8_t data)
{
#ifdef VCS_CART_HOST_MAPPER
    /* The host's own cartridge class claims these addresses when it needs
     * them -- Stella's CartridgeUA takes $0220/$0240 and CartridgeFE page
     * $01C0 -- so there is nothing for us to watch. */
    (void)m; (void)a; (void)data;
#else
    if (m->map.watch_low)
        (void)vcsmap_serve(&m->map, a, data, true);
#endif
}

/* A store into cart space. Returns what it meant; ev_a and ev_b carry the
 * operands. Plain stores into the painted window are DROPPED rather than
 * allowed to corrupt a reply the console is halfway through reading. */
static inline vcs_ev_t vcs_write(vcs_mem_t *m, uint16_t a, uint8_t v,
                                 uint8_t *ev_a, uint8_t *ev_b)
{
#ifndef VCS_CART_HOST_MAPPER
    if (m->map.kind != VCSMAP_FUJI) {
        /* A booted game: its mapper decodes the store by address, as real boards do. */
        vcsmap_write(&m->map, a, v, true);
        return VCS_EV_NONE;
    }
#endif

    if (!vcs_selected(a) || !m->mailbox)
        return VCS_EV_NONE;

    uint16_t off = vcs_off(a);
    uint16_t page = (uint16_t)(off & 0x0F00u);

    if (page == (FN_TX_BASE & 0x0FFFu)) {
        if (!m->armed)
            return VCS_EV_NONE;
        /* Anywhere in the page appends, and `sta $1E00,x` cannot carry, so its dummy read
         * lands on this same inert page. */
        *ev_b = v;
        return VCS_EV_TX;
    }

    if (page != (FN_H_REGSEL & 0x0FFFu))
        return VCS_EV_NONE;   /* painted window and banked ROM swallow stores */

    uint8_t low = (uint8_t)(off & 0xFFu);

    /* The arming gate; see FN_ARM_MAGIC1 in fuji_mailbox.h. */
    if (low == FN_HOT_ARM1) {
        m->arm_step = (v == FN_ARM_MAGIC1) ? 1 : 0;
        return VCS_EV_NONE;
    }
    if (low == FN_HOT_ARM2) {
        if (m->arm_step == 1 && v == FN_ARM_MAGIC2) {
            m->arm_step = 0;
            if (!m->armed) {
                m->armed = true;
                return VCS_EV_ARMED;
            }
        } else {
            m->arm_step = 0;
        }
        return VCS_EV_NONE;
    }
    m->arm_step = 0;
    if (!m->armed)
        return VCS_EV_NONE;

    if (low < 0x80) {
        /* Arm register `low`. The data is deliberately ignored: the value
         * arrives with the commit, so a lone stray cannot carry one. */
        m->regsel = low;
        m->regsel_armed = true;
        return VCS_EV_NONE;
    }

    /* One-shot operations never disturb an armed register, so a stray here cannot break up a
     * pair (fujimail.c's rule too). */
    if (low >= FN_HOT_BANK && low <= FN_HOT_BANK_LAST) {
        *ev_b = (uint8_t)(low - FN_HOT_BANK);
        return VCS_EV_BANK;
    }

    switch (low) {
    case FN_HOT_TROW:
        m->trow = v;
        m->tlen = 0;
        *ev_b = v;
        return VCS_EV_TROW;
    case FN_HOT_TCHR:
        /* Saturates rather than wrapping: a client that streams an over-long
         * filename gets it truncated, never the next row corrupted. */
        if (m->tlen < FN_T_COLS)
            m->tbuf[m->tlen++] = v;
        *ev_b = v;
        return VCS_EV_TCHR;
    case FN_HOT_TEND:  *ev_b = v; return VCS_EV_TEND;

    case FN_HOT_PATH_CH:
        /* Saturates rather than wrapping, for the same reason FN_HOT_TCHR
         * does: an over-long path is truncated, never a buffer corrupted. */
        if (m->path_job != VCS_JOB_NONE)
            vcs_path_finish(m);
        if (m->path_len[m->path_sel] < FN_PATH_MAX)
            m->path[m->path_sel][m->path_len[m->path_sel]++] = v;
        vcs_publish_pathlen(m);
        return VCS_EV_NONE;

    case FN_HOT_PATH_OP:
        /* A pending job lands first, so ops apply in order; a select changes no buffer. */
        if (m->path_job != VCS_JOB_NONE && (v < FN_PATH_SEL0 || v > FN_PATH_SEL3))
            vcs_path_finish(m);
        if (v == FN_PATH_RST) {
            m->path_len[m->path_sel] = 0;
        } else if (v == FN_PATH_POP) {
            /* "/a/b/c/" -> "/a/b/", "/a/" -> "/", "/" -> "/". A directory is
             * held WITH its trailing separator, so appending a name to it is
             * one store and needs no separator logic on the console. */
            uint16_t len = m->path_len[m->path_sel];

            if (len > 1u && m->path[m->path_sel][len - 1u] == (uint8_t)'/')
                len--;
            m->job_dst = m->path_sel;
            m->job_pos = len;
            m->path_job = VCS_JOB_POP;
        } else if (v == FN_PATH_POPCH) {
            /* The editor's backspace; stops at empty. */
            if (m->path_len[m->path_sel] > 0u)
                m->path_len[m->path_sel]--;
        } else if (v >= FN_PATH_SEL0 && v <= FN_PATH_SEL3) {
            m->path_sel = (uint8_t)(v - FN_PATH_SEL0);
        } else if (v == FN_PATH_COMMIT) {
            /* Accept an edit: the selected buffer becomes what was typed. */
            if (m->path_sel != FN_PATH_SCRATCH)
                vcs_path_copy(m, FN_PATH_SCRATCH, m->path_sel);
        } else if (v == FN_PATH_SEED) {
            /* Begin an edit. Cancel is never issuing COMMIT, which leaves the original
             * untouched. */
            if (m->path_sel != FN_PATH_SCRATCH)
                vcs_path_copy(m, m->path_sel, FN_PATH_SCRATCH);
        } else if (v == FN_PATH_TX) {
            return VCS_EV_PATHTX;
        } else if (v == FN_PATH_TXRAW) {
            return VCS_EV_PATHRAW;
        }
#ifndef VCS_CART_DEFER_PATH
        vcs_path_finish(m);     /* MAME, Stella and the host tests have no bus cycle to miss */
#endif
        /* A select changes which length is published, so this has to run for
         * every op, not just the mutating ones. */
        vcs_publish_pathlen(m);
        return VCS_EV_NONE;
    case FN_HOT_BLIT_SL: m->blit_src = (uint16_t)((m->blit_src & 0xFF00u) | v); return VCS_EV_NONE;
    case FN_HOT_BLIT_SH: m->blit_src = (uint16_t)((m->blit_src & 0x00FFu) | ((uint16_t)v << 8)); return VCS_EV_NONE;
    case FN_HOT_BLIT_DL: m->blit_dst = (uint16_t)((m->blit_dst & 0xFF00u) | v); return VCS_EV_NONE;
    case FN_HOT_BLIT_DH: m->blit_dst = (uint16_t)((m->blit_dst & 0x00FFu) | ((uint16_t)v << 8)); return VCS_EV_NONE;
    case FN_HOT_BLIT_CNT: m->blit_cnt = v; return VCS_EV_NONE;
    case FN_HOT_BLIT_GO:
        if (m->path_job != VCS_JOB_NONE)
            vcs_path_finish(m); /* core0 may be about to render a path buffer */
        *ev_b = v;
        return VCS_EV_BLIT;
    case FN_HOT_SWAP:  return m->swap_armed ? VCS_EV_SWAP : VCS_EV_NONE;
    case FN_HOT_COMMIT:
        /* A commit with nothing armed is a no-op, and the arm is spent. */
        if (!m->regsel_armed)
            return VCS_EV_NONE;
        m->regsel_armed = false;
        *ev_a = m->regsel;
        *ev_b = v;
        return VCS_EV_REG;
    default:           return VCS_EV_NONE;
    }
}

/* Map bank b of the image into $1000-$17FF. The high 2K is never banked, so $1FFC is reachable
 * from every bank and RESET is survivable. One pointer assignment, because core1 runs this
 * inline and must finish before the next fetch. An out-of-range bank serves open bus rather
 * than folding onto something that looks like code. */
static inline void vcs_set_bank(vcs_mem_t *m, uint8_t b)
{
    uint32_t base = (uint32_t)b * FN_BANK_SIZE;

    m->bank = b;
    if (m->image && base + FN_BANK_SIZE <= m->image_size - FN_BANK_SIZE)
        m->lowbank = m->image + base;
    else if (m->image && m->image_size == 2u * FN_BANK_SIZE && b == 0)
        m->lowbank = m->image;          /* a flat client: one bank, and it is 0 */
    else
        m->lowbank = NULL;              /* open bus */
    m->win[FN_B_BANK - FN_WINDOW_BASE] = b;
}

/* Serve `image`. The fixed half is always the LAST 2K of the image, so a
 * client is (N+1) * 2048 bytes: N banks then the fixed half. */
static inline void vcs_set_image(vcs_mem_t *m, const uint8_t *img, uint32_t len)
{
    m->image = img;
    m->image_size = len;

    memset(m->win, 0xFF, FN_WINDOW_SIZE);
    if (img && len >= FN_BANK_SIZE)
        memcpy(m->win + FN_BANK_SIZE, img + (len - FN_BANK_SIZE), FN_BANK_SIZE);

    /* "FUJI" promises the mailbox pages are not the image's own code, so the mailbox stays
     * live after it boots. */
    m->mailbox = img && len >= FN_BANK_SIZE &&
                 memcmp(m->win + (FN_R_CLAIM - FN_WINDOW_BASE),
                        FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
    m->armed = false;
    m->arm_step = 0;
    m->swap_armed = false;
    m->regsel_armed = false;
    vcs_set_bank(m, 0);
    m->win[FN_B_FLAGS - FN_WINDOW_BASE] = m->mailbox ? 0x02 : 0x00;

    /* A client gets the FujiNet layout; anything else is a game on the board it shipped on. */
#ifndef VCS_CART_HOST_MAPPER
    if (m->mailbox) {
        vcsmap_init(&m->map, VCSMAP_FUJI, img, len, false);
    } else {
        /* The .cfg hint wins, because size alone cannot tell an 8K F8 from E0, UA or FE;
         * otherwise size, as MAME's identify_cart_type() starts. */
        vcsmap_kind_t k = m->hint;
        bool sc = m->hint_sc;
        if (k == VCSMAP_NONE) {
            k = vcsmap_detect(img, len);
            sc = vcsmap_has_superchip(img, len);
        }
        vcsmap_init(&m->map, k, img, len, sc);
    }
#endif
    /* Spend the hint: a mount with no .cfg sibling sends none, and a stale hint would apply to
     * the next image. */
#ifdef VCS_CART_HOST_MAPPER
    m->cfg_name[0] = '\0';
#else
    m->hint = VCSMAP_NONE;
    m->hint_sc = false;
#endif
}

/* Take the scheme from a .cfg sibling. Applies to the next image served and
 * to that one only. */
static inline void vcs_set_cfg(vcs_mem_t *m, const char *cfg, unsigned len)
{
#ifdef VCS_CART_HOST_MAPPER
    unsigned n = 0;

    if (cfg) {
        /* Trim at the first control character: the sibling is a text file and
         * arrives with whatever line ending it was written with. */
        while (n < len && n + 1 < VCS_CART_CFG_MAX &&
               (unsigned char)cfg[n] > 0x20u)
            n++;
        memcpy(m->cfg_name, cfg, n);
    }
    m->cfg_name[n] = '\0';
#else
    m->hint = (cfg && len) ? vcsmap_from_name(cfg, len) : VCSMAP_NONE;
    m->hint_sc = (m->hint != VCSMAP_NONE) && vcsmap_name_is_sc(cfg, len);
#endif
}

#endif /* VCS_CART_H */
