/* test_pathjob.c -- COMMIT, SEED and POP as core1 runs them, a step at a time
 * (VCS_CART_DEFER_PATH). With jobs left pending between ops, every op must still see what the
 * old inline code produced, and the store itself must never walk a buffer.
 */

#include <stdint.h>
#include <string.h>

#include "check.h"
#include "fuji_mailbox.h"
#include "vcs_cart.h"

static uint8_t image[4 * FN_BANK_SIZE];
static vcs_mem_t mem;
static vcs_mem_t done;          /* a copy of mem with its job run out */

/* The inline semantics before deferral, as the reference. */
static struct {
    uint8_t path[FN_PATH_BUFS][FN_PATH_MAX];
    uint16_t len[FN_PATH_BUFS];
    uint8_t sel;
} ref;

static void ref_char(uint8_t chr)
{
    if (ref.len[ref.sel] < FN_PATH_MAX)
        ref.path[ref.sel][ref.len[ref.sel]++] = chr;
}

static void ref_copy(uint8_t src, uint8_t dst)
{
    memcpy(ref.path[dst], ref.path[src], ref.len[src]);
    ref.len[dst] = ref.len[src];
}

static void ref_op(uint8_t code)
{
    uint16_t *len = &ref.len[ref.sel];
    const uint8_t *buf = ref.path[ref.sel];

    if (code == FN_PATH_RST) {
        *len = 0;
    } else if (code == FN_PATH_POP) {
        if (*len > 1u && buf[*len - 1u] == '/')
            (*len)--;
        while (*len > 1u && buf[*len - 1u] != '/')
            (*len)--;
    } else if (code == FN_PATH_POPCH) {
        if (*len > 0u)
            (*len)--;
    } else if (code >= FN_PATH_SEL0 && code <= FN_PATH_SEL3) {
        ref.sel = (uint8_t)(code - FN_PATH_SEL0);
    } else if (code == FN_PATH_COMMIT && ref.sel != FN_PATH_SCRATCH) {
        ref_copy(FN_PATH_SCRATCH, ref.sel);
    } else if (code == FN_PATH_SEED && ref.sel != FN_PATH_SCRATCH) {
        ref_copy(ref.sel, FN_PATH_SCRATCH);
    }
}

static vcs_ev_t store(uint8_t low, uint8_t val)
{
    uint8_t evx = 0, evy = 0;

    return vcs_write(&mem, (uint16_t)(FN_H_REGSEL + low), val, &evx, &evy);
}

static void setup(void)
{
    memset(image, 0, sizeof image);
    memcpy(image + sizeof image - FN_BANK_SIZE + (FN_R_CLAIM - FN_FIXED_BASE),
           FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    memset(&mem, 0, sizeof mem);
    memset(&ref, 0, sizeof ref);
    vcs_set_image(&mem, image, sizeof image);
    store(FN_HOT_ARM1, FN_ARM_MAGIC1);
    store(FN_HOT_ARM2, FN_ARM_MAGIC2);
}

static unsigned published(const vcs_mem_t *cart)
{
    return cart->win[FN_B_PATHLEN - FN_WINDOW_BASE]
         | (unsigned)cart->win[FN_B_PATHLEN + 1 - FN_WINDOW_BASE] << 8;
}

/* What the console and core0 see once the job lands: every buffer, and the published length. */
static int matches(void)
{
    unsigned buf;

    done = mem;
    vcs_path_finish(&done);
    if (done.path_sel != ref.sel || published(&done) != ref.len[ref.sel])
        return 0;
    for (buf = 0; buf < FN_PATH_BUFS; buf++)
        if (done.path_len[buf] != ref.len[buf]
            || memcmp(done.path[buf], ref.path[buf], ref.len[buf]) != 0)
            return 0;
    return 1;
}

static void push(const char *str)
{
    for (; *str; str++) {
        store(FN_HOT_PATH_CH, (uint8_t)*str);
        ref_char((uint8_t)*str);
    }
}

static void path_op(uint8_t code)
{
    store(FN_HOT_PATH_OP, code);
    ref_op(code);
}

/* The store only starts the walk; each step does a bounded piece of it. */
static void test_deferred(void)
{
    static const char pass[] = "correct-horse-battery-staple-correct-horse-battery-staple-12345";

    setup();
    path_op(FN_PATH_SEL3);
    push(pass);
    path_op(FN_PATH_SEL1);
    path_op(FN_PATH_COMMIT);
    CHECK_EQ(mem.path_job, VCS_JOB_COPY);
    CHECK_EQ(published(&mem), 63);
    vcs_path_step(&mem);
    CHECK_EQ(mem.job_pos, VCS_JOB_COPY_STEP);
    CHECK(matches());

    /* A select neither waits for the job nor publishes a stale length. */
    path_op(FN_PATH_SEL2);
    CHECK_EQ(mem.path_job, VCS_JOB_COPY);
    CHECK_EQ(published(&mem), 0);

    /* Any other op lands the job first. */
    path_op(FN_PATH_SEL1);
    push("!");
    CHECK_EQ(mem.path_job, VCS_JOB_NONE);
    CHECK(matches());

    /* POP publishes when the walk ends, not before. */
    path_op(FN_PATH_SEL0);
    path_op(FN_PATH_RST);
    push("/games/atari/a-long-file-name.bin");
    path_op(FN_PATH_POP);
    CHECK_EQ(mem.path_job, VCS_JOB_POP);
    CHECK_EQ(published(&mem), 33);
    vcs_path_step(&mem);
    CHECK_EQ(mem.job_pos, 33 - VCS_JOB_POP_STEP);
    vcs_path_finish(&mem);
    CHECK_EQ(published(&mem), 13);
    CHECK(matches());

    /* A blit or a TX of the path may read the buffer on core0, so both land the job. */
    path_op(FN_PATH_SEED);
    store(FN_HOT_BLIT_GO, FN_BLIT_PATH);
    CHECK_EQ(mem.path_job, VCS_JOB_NONE);
    path_op(FN_PATH_POP);
    CHECK_EQ(store(FN_HOT_PATH_OP, FN_PATH_TXRAW), VCS_EV_PATHRAW);
    CHECK_EQ(mem.path_job, VCS_JOB_NONE);
    CHECK(matches());
}

static uint32_t seed = 2600;

static unsigned pick(unsigned limit)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) % limit;
}

/* Random ops with 0-3 steps between them, as core1 gets between stores. */
static void test_fuzz(void)
{
    static const uint8_t ops[] = {
        FN_PATH_RST, FN_PATH_POP, FN_PATH_POP, FN_PATH_POPCH, FN_PATH_COMMIT, FN_PATH_COMMIT,
        FN_PATH_SEED, FN_PATH_SEED, FN_PATH_SEL0, FN_PATH_SEL1, FN_PATH_SEL2, FN_PATH_SEL3,
        FN_PATH_TX, FN_PATH_TXRAW,
    };
    unsigned iter, steps, before, bad = 0;

    setup();
    for (iter = 0; iter < 200000u && !bad; iter++) {
        unsigned roll = pick(100);

        if (roll < 50) {
            push(pick(3) ? (pick(2) ? "a" : "b") : "/");
        } else if (roll < 52) {
            push("/a-directory-name-long-enough-to-take-many-steps/");
        } else if (roll < 54) {
            store(FN_HOT_BLIT_GO, FN_BLIT_PATH);
            CHECK_EQ(mem.path_job, VCS_JOB_NONE);
        } else {
            uint8_t code = ops[pick(sizeof ops)];

            path_op(code);
            if ((code == FN_PATH_COMMIT || code == FN_PATH_SEED)
                && mem.path_sel != FN_PATH_SCRATCH && mem.job_len > VCS_JOB_COPY_STEP)
                CHECK_EQ(mem.path_job, VCS_JOB_COPY);
        }
        for (steps = pick(4); steps > 0; steps--) {
            before = mem.job_pos;
            vcs_path_step(&mem);
            if (mem.path_job == VCS_JOB_COPY)
                CHECK_EQ(mem.job_pos - before, VCS_JOB_COPY_STEP);
            else if (mem.path_job == VCS_JOB_POP)
                CHECK(before - mem.job_pos <= VCS_JOB_POP_STEP);
        }
        if (!matches()) {
            fprintf(stderr, "fuzz: diverged from the inline reference at op %u\n", iter);
            bad = 1;
            check_fails++;
        }
    }
}

int main(void)
{
    test_deferred();
    test_fuzz();
    return check_done("test_pathjob");
}
