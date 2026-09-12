/* test_busio.c -- the cartridge's bus decode, and the write-sampling model.
 *
 * This is the rung MAME structurally cannot cover. Its VCS cart device is
 * handed a clean data byte on a write (write8sm_delegate), so the data_prev
 * sampling the real cartridge depends on never runs there at all -- and the
 * arming gate, the arm/commit interlock and the tri-state pages are all about
 * accesses an emulator models as clean reads and writes.
 *
 * The expectations below are written from the RULES IN PROSE in
 * fuji_mailbox.h and vcs_cart.h, never by calling the decoder to find out what
 * it does. That is the discipline the ColecoVision port arrived at with five
 * mappers: an expectation derived from the code under test proves only that
 * the code agrees with itself.
 */

#include <stdio.h>
#include <string.h>

#include "fuji_mailbox.h"
#include "vcs_cart.h"

static int fails;
#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); fails++;                     \
        }                                                       \
    } while (0)

static uint8_t image[4 * FN_BANK_SIZE];
static vcs_mem_t m;

static vcs_ev_t wr(uint16_t a, uint8_t v, uint8_t *p, uint8_t *q)
{
    uint8_t x = 0, y = 0;
    vcs_ev_t e = vcs_write(&m, a, v, &x, &y);
    if (p) *p = x;
    if (q) *q = y;
    return e;
}

/* Open the decode gate the way a client does. */
static void arm(void)
{
    wr(FN_H_REGSEL + FN_HOT_ARM1, FN_ARM_MAGIC1, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_ARM2, FN_ARM_MAGIC2, 0, 0);
}

static void reset_image(int claim)
{
    memset(image, 0, sizeof image);
    /* The fixed half is the LAST 2K of the image; the claim sits inside it. */
    if (claim)
        memcpy(image + sizeof image - FN_BANK_SIZE
                     + (FN_R_CLAIM - FN_FIXED_BASE),
               FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    /* Something recognisable in each bank, to prove banking moves the window. */
    for (unsigned b = 0; b < sizeof image / FN_BANK_SIZE; b++)
        image[b * FN_BANK_SIZE] = (uint8_t)(0xB0 + b);
    memset(&m, 0, sizeof m);
    vcs_set_image(&m, image, sizeof image);
}

static void test_geometry(void)
{
    /* A12 is the entire chip select: everything below $1000 is not ours. */
    CHECK(!vcs_selected(0x0000) && !vcs_selected(0x0FFF),
          "A12 low must not select the cartridge");
    CHECK(vcs_selected(0x1000) && vcs_selected(0x1FFF),
          "A12 high must select the cartridge");

    /* Exactly two pages are write-only, and they are the two the client is
     * forbidden to read. Everything else the cart drives. */
    for (unsigned p = 0; p < 16; p++) {
        uint16_t a = (uint16_t)(0x1000u + p * 0x100u);
        int want = (a == FN_H_REGSEL) || (a == FN_TX_BASE);
        CHECK(vcs_tristate(a) == want,
              "page $%04X tri-state: want %d", a, want);
    }

    /* The reset vector must live in the fixed half, or a console RESET with a
     * non-zero bank mapped would fetch from whatever happened to be there. */
    CHECK(FN_VEC_RESET >= FN_FIXED_BASE, "the reset vector is in the banked half");
    CHECK(FN_R_CLAIM >= FN_FIXED_BASE, "the claim is in the banked half");
    /* fujimail_paint() zeroes one contiguous run, so these must be ordered. */
    CHECK(FN_R_DATA < FN_R_BASE && FN_R_BASE < FN_R_PAINT_END,
          "the paint span is not ordered");
    CHECK(FN_R_PAINT_END <= FN_R_CLAIM, "painting would erase the claim");
    CHECK(FN_R_NSLICES * FN_R_SLICE_LEN == 1024,
          "slices do not add up to FUJIMAIL_RX_MAX");
}

static void test_arming_gate(void)
{
    uint8_t a8, b8;

    reset_image(1);
    CHECK(m.mailbox, "a claiming image must bring the mailbox up");
    CHECK(!m.armed, "the decode gate must start closed");

    /* Closed, nothing decodes -- this is the 7800-BIOS probe defence. */
    wr(FN_H_REGSEL + FN_REG_DEVICE, 0x70, 0, 0);
    CHECK(wr(FN_H_COMMIT, 0x70, &a8, &b8) == VCS_EV_NONE,
          "a register write decoded before the gate opened");
    CHECK(wr(FN_TX_BASE, 0x5A, 0, 0) == VCS_EV_NONE,
          "a TX append decoded before the gate opened");

    /* The wrong values, or the wrong order, must not open it. */
    wr(FN_H_REGSEL + FN_HOT_ARM1, 0x00, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_ARM2, FN_ARM_MAGIC2, 0, 0);
    CHECK(!m.armed, "the gate opened on a wrong first magic");
    wr(FN_H_REGSEL + FN_HOT_ARM2, FN_ARM_MAGIC2, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_ARM1, FN_ARM_MAGIC1, 0, 0);
    CHECK(!m.armed, "the gate opened on a reversed sequence");
    /* Anything in between breaks the pair. */
    wr(FN_H_REGSEL + FN_HOT_ARM1, FN_ARM_MAGIC1, 0, 0);
    wr(FN_H_REGSEL + FN_REG_CMD, 0x00, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_ARM2, FN_ARM_MAGIC2, 0, 0);
    CHECK(!m.armed, "an intervening store did not break the arming pair");

    CHECK(wr(FN_H_REGSEL + FN_HOT_ARM1, FN_ARM_MAGIC1, 0, 0) == VCS_EV_NONE,
          "the first arming store is not an event by itself");
    CHECK(wr(FN_H_REGSEL + FN_HOT_ARM2, FN_ARM_MAGIC2, 0, 0) == VCS_EV_ARMED,
          "the ordered pair did not open the gate");
    CHECK(m.armed, "the gate did not latch open");
}

static void test_arm_commit(void)
{
    uint8_t reg, val;

    reset_image(1);
    arm();

    /* The ordinary shape: arm, then commit carrying the value. */
    CHECK(wr(FN_H_REGSEL + FN_REG_DEVICE, 0xFF, 0, 0) == VCS_EV_NONE,
          "arming is not itself a register write");
    CHECK(wr(FN_H_COMMIT, 0x70, &reg, &val) == VCS_EV_REG,
          "the commit did not produce a register write");
    CHECK(reg == FN_REG_DEVICE && val == 0x70,
          "register write carried reg $%02X val $%02X", reg, val);

    /* ONE stray access must mutate nothing. A commit with nothing armed is a
     * no-op: the arm above was spent. */
    CHECK(wr(FN_H_COMMIT, 0x99, &reg, &val) == VCS_EV_NONE,
          "a second commit reused a spent arm");

    /* An arm with no commit is simply replaced by the next arm. */
    wr(FN_H_REGSEL + FN_REG_CMD, 0, 0, 0);
    wr(FN_H_REGSEL + FN_REG_NPARAM, 0, 0, 0);
    CHECK(wr(FN_H_COMMIT, 0x03, &reg, &val) == VCS_EV_REG && reg == FN_REG_NPARAM,
          "a stale arm survived a later one");

    /* A one-shot operation must NOT disturb an armed register. */
    wr(FN_H_REGSEL + FN_REG_CMD, 0, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_BANK + 1, 0, 0, 0);
    CHECK(wr(FN_H_COMMIT, 0xC4, &reg, &val) == VCS_EV_REG
          && reg == FN_REG_CMD && val == 0xC4,
          "a bank select broke up an armed register pair");
}

static void test_tx_and_ops(void)
{
    uint8_t reg, val;

    reset_image(1);
    arm();

    /* A write ANYWHERE in the TX page appends, so `sta $1E00,x` works for any
     * X -- and the base low byte is $00, so the index cannot carry and the
     * dummy read STA abs,X always performs lands on the same address. */
    for (unsigned i = 0; i < 256; i += 37) {
        CHECK(wr((uint16_t)(FN_TX_BASE + i), (uint8_t)i, &reg, &val) == VCS_EV_TX
              && val == (uint8_t)i,
              "TX append failed at offset %u", i);
    }

    /* Bank select covers exactly the declared range and nothing else. */
    CHECK(wr(FN_H_REGSEL + FN_HOT_BANK, 0, &reg, &val) == VCS_EV_BANK && val == 0,
          "bank 0 select");
    CHECK(wr(FN_H_REGSEL + FN_HOT_BANK_LAST, 0, &reg, &val) == VCS_EV_BANK
          && val == FN_HOT_BANK_LAST - FN_HOT_BANK, "last bank select");
    CHECK(wr(FN_H_REGSEL + FN_HOT_BANK_LAST + 1, 0, &reg, &val) != VCS_EV_BANK,
          "past the last bank still decoded as a bank select");

    /* The swap is armed-only: an unarmed hotspot must be inert, so a runaway
     * cannot replace the image out from under a running client. */
    CHECK(wr(FN_H_REGSEL + FN_HOT_SWAP, 0, 0, 0) == VCS_EV_NONE,
          "an unarmed swap fired");
    m.swap_armed = true;
    CHECK(wr(FN_H_REGSEL + FN_HOT_SWAP, 0, 0, 0) == VCS_EV_SWAP,
          "an armed swap did not fire");

    /* Render and blit ops. */
    CHECK(wr(FN_H_REGSEL + FN_HOT_TROW, 7, &reg, &val) == VCS_EV_TROW && val == 7,
          "text row select");
    CHECK(wr(FN_H_REGSEL + FN_HOT_TCHR, 'A', &reg, &val) == VCS_EV_TCHR && val == 'A',
          "text character append");
    CHECK(wr(FN_H_REGSEL + FN_HOT_TEND, 0, &reg, &val) == VCS_EV_TEND,
          "text row render");
    wr(FN_H_REGSEL + FN_HOT_BLIT_SL, 0x34, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_BLIT_SH, 0x12, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_BLIT_DL, 0x78, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_BLIT_DH, 0x56, 0, 0);
    wr(FN_H_REGSEL + FN_HOT_BLIT_CNT, 0x40, 0, 0);
    CHECK(m.blit_src == 0x1234 && m.blit_dst == 0x5678 && m.blit_cnt == 0x40,
          "blit parameters latched as src $%04X dst $%04X cnt $%02X",
          m.blit_src, m.blit_dst, m.blit_cnt);
    CHECK(wr(FN_H_REGSEL + FN_HOT_BLIT_GO, FN_BLIT_TEXT, &reg, &val) == VCS_EV_BLIT
          && val == FN_BLIT_TEXT, "blit fire");
}

static void test_painted_window_is_read_only(void)
{
    reset_image(1);
    arm();

    /* A console store into the painted window must be DROPPED, not allowed to
     * corrupt a reply the client is halfway through reading. */
    uint16_t probes[] = { FN_T_BASE, FN_R_DATA, FN_R_BASE, FN_R_CLAIM, 0x1000 };
    for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        uint8_t before = m.win[probes[i] - FN_WINDOW_BASE];
        CHECK(wr(probes[i], 0x5A, 0, 0) == VCS_EV_NONE,
              "a store at $%04X decoded as an event", probes[i]);
        CHECK(m.win[probes[i] - FN_WINDOW_BASE] == before,
              "a store at $%04X changed the served window", probes[i]);
    }
}

static void test_claim_and_banking(void)
{
    /* An image with no claim leaves the mailbox dead for the session. */
    reset_image(0);
    CHECK(!m.mailbox, "an unclaiming image brought the mailbox up");
    arm();
    CHECK(!m.armed, "the gate opened on an unclaiming image");
    CHECK(wr(FN_TX_BASE, 0x5A, 0, 0) == VCS_EV_NONE,
          "an unclaiming image still decoded a TX append");

    /* Banking moves the low 2K and leaves the fixed half alone. */
    reset_image(1);
    CHECK(vcs_read(&m, FN_BANK_BASE) == 0xB0, "bank 0 is not mapped at reset");
    uint8_t fixed_before = vcs_read(&m, FN_R_CLAIM);
    vcs_set_bank(&m, 2);
    CHECK(vcs_read(&m, FN_BANK_BASE) == 0xB2, "bank 2 is not being served");
    CHECK(vcs_read(&m, FN_R_CLAIM) == fixed_before,
          "a bank switch disturbed the fixed half");
    CHECK(vcs_read(&m, FN_B_BANK) == 2, "the live bank is not published");
    /* Out of range serves open bus rather than folding onto something that
     * looks like code. */
    vcs_set_bank(&m, 200);
    CHECK(vcs_read(&m, FN_BANK_BASE) == 0xFF,
          "an out-of-range bank served image bytes");
    /* The fixed half is not a selectable bank: it carries the mailbox. */
    vcs_set_bank(&m, (uint8_t)(sizeof image / FN_BANK_SIZE - 1));
    CHECK(vcs_read(&m, FN_BANK_BASE) == 0xFF,
          "the fixed half was selectable as a bank");
}

/* ---- the write-sampling model -------------------------------------------
 *
 * The cart samples D0-D7 while the address is parked and keeps the
 * second-to-last sample. What it sees depends on the SHAPE of the access, and
 * the shapes below are the 6502's, from the addressing modes a client can
 * actually aim at a write port.
 *
 * `float_v` is whatever the undriven bus holds: on a write-only page the cart
 * never drives, so a read cycle there leaves the previous bus contents. The
 * exact value does not matter and the tests do not depend on it -- that it is
 * NOT the stored byte is the whole point.
 */
#define TURNAROUND 0xA5u        /* the bus mid-turnover, after the last cycle */

static unsigned shape_sta_abs(uint8_t *s, uint8_t d)
{
    /* One bus cycle: the CPU drives the data for the whole of it. */
    unsigned n = 0;
    for (unsigned i = 0; i < 30; i++) s[n++] = d;
    s[n++] = TURNAROUND;
    return n;
}

static unsigned shape_sta_abs_x(uint8_t *s, uint8_t d, uint8_t floatv)
{
    /* STA abs,X always performs a dummy READ of the unfixed address before
     * the write. With a page-aligned base the index cannot carry, so both
     * cycles land on the SAME address and the cart sees one long parked
     * access: float, then the driven data. */
    unsigned n = 0;
    for (unsigned i = 0; i < 30; i++) s[n++] = floatv;
    for (unsigned i = 0; i < 30; i++) s[n++] = d;
    s[n++] = TURNAROUND;
    return n;
}

static unsigned shape_rmw(uint8_t *s, uint8_t oldv, uint8_t newv, uint8_t floatv)
{
    /* INC/DEC/ASL/LSR/ROL/ROR abs are THREE cycles at one address: read,
     * write the old value, write the new one. */
    unsigned n = 0;
    for (unsigned i = 0; i < 30; i++) s[n++] = floatv;
    for (unsigned i = 0; i < 30; i++) s[n++] = oldv;
    for (unsigned i = 0; i < 30; i++) s[n++] = newv;
    s[n++] = TURNAROUND;
    return n;
}

static void test_sampling(void)
{
    uint8_t s[256];
    unsigned n;

    /* The plain store, and the indexed store a TX append actually uses, must
     * recover the same byte -- that equivalence is what makes `sta $1E00,x`
     * legal for any X. */
    for (unsigned d = 0; d < 256; d += 17) {
        n = shape_sta_abs(s, (uint8_t)d);
        CHECK(vcs_recover(s, n) == (uint8_t)d,
              "STA abs recovered $%02X, want $%02X", vcs_recover(s, n), d);
        n = shape_sta_abs_x(s, (uint8_t)d, 0xFF);
        CHECK(vcs_recover(s, n) == (uint8_t)d,
              "STA abs,X recovered $%02X, want $%02X", vcs_recover(s, n), d);
    }

    /* And the hazard, stated positively. An RMW's three cycles put the OLD
     * value on the bus before the new one, so what the cart appends depends on
     * where in that burst the address happens to change -- and the "old" value
     * an RMW reads back from a write-only page is open bus, not anything the
     * programmer chose. There is no correct answer here, which is why
     * tools/checkrom.py rejects the opcodes outright rather than trying to
     * make them work. */
    n = shape_rmw(s, 0x00, 0x01, 0xFF);
    CHECK(vcs_recover(s, n) != 0x00,
          "the RMW model happens to recover the old value; the test is wrong");

    /* Degenerate sequences must not read off the front of the buffer. */
    s[0] = 0x42;
    CHECK(vcs_recover(s, 1) == 0x42, "a one-sample access");
    CHECK(vcs_recover(s, 0) == 0xFF, "a zero-sample access must not underflow");
}

/* A booted GAME is served by its real board, not by our window. This is the
 * seam between the two worlds and it is worth an explicit test: the claim is
 * what decides, and getting it wrong means either a game that does not bank or
 * a client whose mailbox is decoded as a bankswitch hotspot. */
static void test_booted_game(void)
{
    static uint8_t game[8192];
    unsigned i;

    for (i = 0; i < sizeof game; i++)
        game[i] = (uint8_t)(i >> 12);      /* every byte = its bank number */

    memset(&m, 0, sizeof m);
    vcs_set_image(&m, game, sizeof game);

    CHECK(!m.mailbox, "an 8K game with no claim brought the mailbox up");
    CHECK(m.map.kind == VCSMAP_F8, "an 8K image was not detected as F8");

    /* It banks, and the hotspot returns the OLD bank -- MAME's read tap runs
     * after the read. */
    CHECK(vcs_read(&m, 0x1000) == 0, "bank 0 is not served at reset");
    CHECK(vcs_read(&m, 0x1FF9) == 0, "the hotspot returned the new bank");
    CHECK(vcs_read(&m, 0x1000) == 1, "the hotspot did not switch the bank");

    /* A debugger peek must not move it. */
    vcs_read_ex(&m, 0x1FF8, false);
    CHECK(vcs_read(&m, 0x1000) == 1, "a non-committing read moved the bank");

    /* And the write-only pages are NOT tri-stated for a game: $1D and $1E are
     * ours, not the board's, and a game expects ROM there. */
    CHECK(vcs_read(&m, 0x1D00) != 0xFF || game[0x1D00] == 0xFF,
          "a booted game was denied its own $1D00");
    CHECK(vcs_read(&m, 0x1E00) == 1, "a booted game was denied its own $1E00");
}

int main(void)
{
    test_geometry();
    test_arming_gate();
    test_arm_commit();
    test_tx_and_ops();
    test_painted_window_is_read_only();
    test_claim_and_banking();
    test_sampling();
    test_booted_game();

    if (fails) {
        printf("test_busio: %d failures\n", fails);
        return 1;
    }
    printf("test_busio: PASS (decode, arming gate, arm/commit interlock, "
           "banking, claim, booted games, and the write-sampling model)\n");
    return 0;
}
