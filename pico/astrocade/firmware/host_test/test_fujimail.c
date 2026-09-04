/* test_fujimail.c -- desktop regression tests for the hotspot mailbox
 * service: register decode through read pairs, the SEQ/ACKSEQ interlock, the
 * SLICE_ECHO repaint handshake, the DBC push receiver, and -- the reason this
 * test exists at all -- proof that stray reads (Z80 refresh sweeps, debugger
 * dumps) can never mutate a register, launch a transaction, or reboot the
 * cart. MAME cannot model refresh and its socket transport is synchronous,
 * so this is the only place those cases run.
 *
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_fujimail \
 *            test_fujimail.c ../src/fujimail.c ../src/astromap.c
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fujimail.h"
#include "fuji_mailbox.h"
#include "astromap.h"

/* ---- the stub port ---- */

static uint8_t window[FN_WINDOW_SIZE];
static unsigned long poke_clock;
static unsigned long poke_when[FN_WINDOW_SIZE];

static int link_state = 1;

static struct {
    unsigned calls;
    uint8_t device, command;
    fb_param_t params[8];
    unsigned nparams;
    uint8_t payload[FN_TX_MAX];
    uint16_t payload_len;
    uint32_t timeout_ms;
} txn;

static uint8_t reply_data[FUJIMAIL_RX_MAX + 64];
static uint16_t reply_len;
static fb_status_t reply_status = FB_OK;

static unsigned armed_calls;
static unsigned bootsel_calls;

/* Port-owned stream storage, big enough for a 512K game image, with
 * scriptable failures: this is the fixture the hardware store and the MAME
 * device both mirror. */
static uint8_t store[ASTROMAP_GAME512_SIZE];
static struct {
    unsigned opens, writes, closes;
    int stream;
    uint32_t open_size;
    uint32_t len;
    int aborted;
    uint8_t open_err;       /* scripted refusal on top of the gate */
    uint8_t close_err;      /* scripted commit failure (a NOMAP etc.) */
} pushed;

static void stub_poke(unsigned offset, uint8_t v)
{
    assert(offset >= FN_R_DATA && offset < FN_R_PAINT_END);
    window[offset] = v;
    poke_when[offset] = ++poke_clock;
}

static bool stub_link_up(void) { return link_state != 0; }

static fb_status_t stub_transact(uint8_t device, uint8_t command,
                                 const fb_param_t *params, unsigned nparams,
                                 const uint8_t *payload, uint16_t payload_len,
                                 uint32_t timeout_ms, fb_reply_t *reply)
{
    txn.calls++;
    txn.device = device;
    txn.command = command;
    txn.nparams = nparams;
    memcpy(txn.params, params, nparams * sizeof *params);
    txn.payload_len = payload_len;
    memcpy(txn.payload, payload, payload_len);
    txn.timeout_ms = timeout_ms;

    reply->device = device;
    reply->command = CMD_FUJI_ACK;
    reply->data = reply_data;
    reply->data_len = reply_len;
    return reply_status;
}

static void stub_send_bare(uint8_t device, uint8_t command,
                           const uint8_t *payload, uint16_t payload_len)
{
    (void) device; (void) command; (void) payload; (void) payload_len;
}

static uint8_t stub_stream_open(int stream, uint32_t size)
{
    pushed.opens++;
    pushed.stream = stream;
    pushed.open_size = size;
    pushed.len = 0;
    if (pushed.open_err)
        return pushed.open_err;
    return (stream == 0) ? astromap_gate(size) : 0;
}

static void stub_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    (void) stream;
    pushed.writes++;
    if (pushed.len + len <= sizeof store) {
        memcpy(store + pushed.len, chunk, len);
        pushed.len += len;
    }
}

static uint8_t stub_stream_close(int stream, uint32_t got, bool aborted)
{
    (void) got;
    pushed.closes++;
    pushed.stream = stream;
    pushed.aborted = aborted;
    return aborted ? 0 : pushed.close_err;
}

static void stub_arm_swap(void) { armed_calls++; }
static void stub_bootsel(void) { bootsel_calls++; }

static const fujimail_port_t stub_port = {
    .poke         = stub_poke,
    .link_up      = stub_link_up,
    .transact     = stub_transact,
    .send_bare    = stub_send_bare,
    .stream_open  = stub_stream_open,
    .stream_write = stub_stream_write,
    .stream_close = stub_stream_close,
    .arm_swap     = stub_arm_swap,
    .wait_link_ms = NULL,
    .bootsel      = stub_bootsel,
    .on_txn       = NULL,
    .on_dbc       = NULL,
};

/* ---- driving helpers, written the way the Z80 client reads ---- */

static void reg_write(uint8_t reg, uint8_t val)
{
    fujimail_read_hotspot((uint16_t)(FN_H_REGSEL + reg));
    fujimail_read_hotspot((uint16_t)(FN_H_REGDATA + val));
}

static void tx_byte(uint8_t val)
{
    fujimail_read_hotspot((uint16_t)(FN_H_DATA + val));
}

static void fresh(void)
{
    memset(window, 0, sizeof window);
    memset(poke_when, 0, sizeof poke_when);
    poke_clock = 0;
    memset(&txn, 0, sizeof txn);
    memset(&pushed, 0, sizeof pushed);
    armed_calls = 0;
    bootsel_calls = 0;
    reply_len = 0;
    reply_status = FB_OK;
    link_state = 1;
    fujimail_init(&stub_port);
    fujimail_paint();
}

/* ---- DBC helpers ---- */

static void dbc_frame(uint8_t command, const uint8_t *data, uint16_t len)
{
    fb_reply_t f;

    f.device = FUJI_DEVICEID_DBC;
    f.command = command;
    f.data = data;
    f.data_len = len;
    assert(fujimail_inbound(&f));
}

static void dbc_open(unsigned stream, uint32_t len)
{
    uint8_t open[5];

    open[0] = (uint8_t) stream;
    open[1] = (uint8_t)(len & 0xFF);
    open[2] = (uint8_t)(len >> 8);
    open[3] = (uint8_t)(len >> 16);
    open[4] = (uint8_t)(len >> 24);
    dbc_frame(CMD_NET_OPEN, open, 5);
}

static void dbc_write_all(const uint8_t *image, uint32_t len)
{
    uint32_t off;

    for (off = 0; off < len; off += 512) {
        uint32_t n = len - off > 512 ? 512 : len - off;

        dbc_frame(CMD_NET_WRITE, image + off, (uint16_t) n);
    }
}

static void dbc_push(const uint8_t *image, uint32_t len)
{
    dbc_open(0, len);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_XFER);
    dbc_write_all(image, len);
    dbc_frame(CMD_NET_CLOSE, NULL, 0);
}

/* ---- tests ---- */

static void test_paint(void)
{
    fresh();
    assert(window[FN_R_MAGIC0] == 'F');
    assert(window[FN_R_MAGIC1] == 'N');
    assert(window[FN_R_PROTO_VER] == FN_PROTO_VER);
    assert(window[FN_R_ACKSEQ] == 0);
    assert(window[FN_R_STATUS] & FN_R_STATUS_LINK);
}

static void run_txn(uint8_t seq, uint8_t device, uint8_t command)
{
    reg_write(FN_REG_DATA_RST, 0);
    reg_write(FN_REG_DEVICE, device);
    reg_write(FN_REG_CMD, command);
    reg_write(FN_REG_NPARAM, 2);
    tx_byte(1); tx_byte(0x42);                          /* param 0: u8   */
    tx_byte(2); tx_byte(0x34); tx_byte(0x12);           /* param 1: u16  */
    tx_byte('h'); tx_byte('i');                         /* payload       */
    reg_write(FN_REG_SEQ, seq);
}

static void test_transaction(void)
{
    unsigned i;

    fresh();
    strcpy((char *) reply_data, "PONG");
    reply_len = 4;

    run_txn(1, 0x70, 0xC4);
    assert(txn.calls == 1);
    assert(txn.device == 0x70 && txn.command == 0xC4);
    assert(txn.nparams == 2);
    assert(txn.params[0].size == 1 && txn.params[0].value == 0x42);
    assert(txn.params[1].size == 2 && txn.params[1].value == 0x1234);
    assert(txn.payload_len == 2 && memcmp(txn.payload, "hi", 2) == 0);
    assert(window[FN_R_ACKSEQ] == 1);
    assert(window[FN_R_ERR] == FN_ERR_OK);
    assert(window[FN_R_REPLY_CMD] == CMD_FUJI_ACK);
    assert(window[FN_R_RXLEN_LO] == 4 && window[FN_R_RXLEN_HI] == 0);
    assert(memcmp(&window[FN_R_DATA], "PONG", 4) == 0);

    /* The interlock: ACKSEQ must be the LAST publish of the transaction. */
    for (i = FN_R_DATA; i < FN_R_PAINT_END; i++)
        if (i != FN_R_ACKSEQ && poke_when[i])
            assert(poke_when[i] < poke_when[FN_R_ACKSEQ]);
}

static void test_seq_gates(void)
{
    fresh();
    run_txn(1, 0x70, 0xC4);
    assert(txn.calls == 1);

    /* Same sequence again: a retry, not a replay. */
    reg_write(FN_REG_SEQ, 1);
    assert(txn.calls == 1);

    /* Sequence 0 is reserved. */
    reg_write(FN_REG_SEQ, 0);
    assert(txn.calls == 1);

    run_txn(2, 0x70, 0xC4);
    assert(txn.calls == 2);
}

static void test_slice_echo(void)
{
    unsigned i;

    fresh();
    for (i = 0; i < sizeof reply_data; i++)
        reply_data[i] = (uint8_t) i;
    reply_len = 700;                    /* spans 3 slices */

    run_txn(1, 0x70, 0xC4);
    assert(window[FN_R_SLICE_ECHO] == 0);
    assert(window[FN_R_DATA + 5] == 5);

    reg_write(FN_REG_RXSLICE, 2);
    assert(window[FN_R_SLICE_ECHO] == 2);
    assert(window[FN_R_DATA + 0] == (uint8_t)(2 * FN_R_SLICE_LEN));
    /* Bytes past rxlen in the last occupied slice read zero. */
    assert(window[FN_R_DATA + (700 - 2 * FN_R_SLICE_LEN)] == 0);

    /* The handshake: the echo must be the LAST publish of the repaint. */
    for (i = 0; i < FN_R_SLICE_LEN; i++)
        assert(poke_when[FN_R_DATA + i] < poke_when[FN_R_SLICE_ECHO]);
}

static void test_unpaired_regdata(void)
{
    fresh();
    run_txn(1, 0x70, 0xC4);

    /* No REGSEL arm: REGDATA reads fall on deaf ears, even SEQ-shaped ones. */
    fujimail_read_hotspot(FN_H_REGDATA + 2);
    fujimail_read_hotspot(FN_H_REGDATA + 3);
    assert(txn.calls == 1);

    /* A special-op read must not disturb an armed regsel... */
    fujimail_read_hotspot(FN_H_REGSEL + FN_REG_DEVICE);
    fujimail_read_hotspot(FN_H_REGSEL + 0xE0);
    fujimail_read_hotspot(FN_H_REGDATA + 0x99);
    /* ...so that pair set DEVICE. Commit without touching DEVICE again. */
    reg_write(FN_REG_CMD, 0x01);
    reg_write(FN_REG_SEQ, 2);
    assert(txn.calls == 2 && txn.device == 0x99);

    /* Bank selects belong to the bus-serving layer; reaching this decoder
     * (as a stray, or a port that forwards them anyway) they are no-ops and
     * leave an armed regsel alone, same as every special op. */
    fujimail_read_hotspot(FN_H_REGSEL + FN_REG_DEVICE);
    fujimail_read_hotspot(FN_H_REGSEL + FN_HOT_BANK);
    fujimail_read_hotspot(FN_H_REGSEL + FN_HOT_BANK_LAST);
    fujimail_read_hotspot(FN_H_REGDATA + 0x33);
    reg_write(FN_REG_CMD, 0x01);
    reg_write(FN_REG_SEQ, 3);
    assert(txn.calls == 3 && txn.device == 0x33);
}

/* A refresh sweep: the R register walks 0x00-0x7F (bit 7 never counts) over
 * whatever page the I register selects. The sweep must not mutate anything
 * observable: no transaction, no swap arm, no reboot, no register change.
 * The bank-select ops live at 0x80+ precisely so no sweep can reach them. */
static void sweep(uint16_t page_base)
{
    unsigned r, lap;

    for (lap = 0; lap < 2; lap++)
        for (r = 0; r < 0x80; r++)
            fujimail_read_hotspot((uint16_t)(page_base + r));
}

static void test_refresh_sweeps(void)
{
    unsigned i;

    fresh();
    strcpy((char *) reply_data, "OK");
    reply_len = 2;
    run_txn(1, 0x70, 0xC4);
    assert(txn.calls == 1);

    sweep(FN_H_REGSEL);
    sweep(FN_H_REGDATA);
    sweep(FN_H_DATA);
    /* Interleaved, as consecutive refresh cycles with a perverse I would be. */
    for (i = 0; i < 0x80; i++) {
        fujimail_read_hotspot((uint16_t)(FN_H_REGSEL + (i * 37 % 0x80)));
        fujimail_read_hotspot((uint16_t)(FN_H_DATA + (i * 53 % 0x80)));
    }

    assert(txn.calls == 1);
    assert(armed_calls == 0);
    assert(bootsel_calls == 0);
    assert(window[FN_R_ACKSEQ] == 1);

    /* The REGSEL page sweep leaves a stale armed regsel behind (0x7F, the
     * last one it touched) -- prove the very next legitimate pair still
     * works and that the stale arm decayed onto a harmless register. */
    run_txn(2, 0x70, 0xC4);
    assert(txn.calls == 2 && txn.device == 0x70);
}

static void test_bootsel_pairing(void)
{
    fresh();

    reg_write(FN_REG_BOOTSEL_2, FN_BOOTSEL_MAGIC2);     /* no first half   */
    assert(bootsel_calls == 0);

    reg_write(FN_REG_BOOTSEL_1, FN_BOOTSEL_MAGIC1);
    reg_write(FN_REG_DEVICE, 0x70);                     /* pair broken     */
    reg_write(FN_REG_BOOTSEL_2, FN_BOOTSEL_MAGIC2);
    assert(bootsel_calls == 0);

    reg_write(FN_REG_BOOTSEL_1, 0x11);                  /* wrong magic     */
    reg_write(FN_REG_BOOTSEL_2, FN_BOOTSEL_MAGIC2);
    assert(bootsel_calls == 0);

    reg_write(FN_REG_BOOTSEL_1, FN_BOOTSEL_MAGIC1);
    reg_write(FN_REG_BOOTSEL_2, FN_BOOTSEL_MAGIC2);
    assert(bootsel_calls == 1);
}

static void test_dbc_push_and_bootlock(void)
{
    static uint8_t image[4096];
    unsigned i;

    fresh();
    for (i = 0; i < sizeof image; i++)
        image[i] = (uint8_t)(i ^ (i >> 5));

    /* BOOTLOCK before anything is staged: refused. */
    reg_write(FN_REG_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    assert(armed_calls == 0);

    dbc_push(image, sizeof image);
    assert(pushed.opens == 1 && pushed.closes == 1);
    assert(pushed.stream == 0 && !pushed.aborted);
    assert(pushed.open_size == sizeof image && pushed.len == sizeof image);
    assert(store[0] == image[0]
           && store[sizeof image - 1] == image[sizeof image - 1]);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_READY);
    assert(window[FN_R_BOOT_PCT] == 100);

    /* Wrong magic: still refused. */
    reg_write(FN_REG_BOOTLOCK, 0x42);
    assert(armed_calls == 0);

    reg_write(FN_REG_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    assert(armed_calls == 1);
}

static void test_dbc_abort_and_toobig(void)
{
    static const uint8_t junk[100] = { 1, 2, 3 };
    uint8_t abortbyte = 0x01;

    fresh();
    dbc_open(0, 100);
    dbc_frame(CMD_NET_WRITE, junk, 50);
    dbc_frame(CMD_NET_CLOSE, &abortbyte, 1);
    assert(pushed.closes == 1 && pushed.aborted);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_FAILED);
    assert(window[FN_R_BOOT_ERR] == FN_BOOT_ERR_TRUNCATED);
    reg_write(FN_REG_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    assert(armed_calls == 0);

    /* An image no scheme could hold is refused at OPEN, before the ESP32
     * drags the whole file over TNFS. */
    fresh();
    dbc_open(0, 0x10001);
    assert(pushed.opens == 1 && pushed.closes == 0);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_FAILED);
    assert(window[FN_R_BOOT_ERR] == FN_BOOT_ERR_TOOBIG);

    /* Store contention is the port's verdict, surfaced the same way. */
    fresh();
    pushed.open_err = FN_BOOT_ERR_STOREBUSY;
    dbc_open(0, 4096);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_FAILED);
    assert(window[FN_R_BOOT_ERR] == FN_BOOT_ERR_STOREBUSY);
    reg_write(FN_REG_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    assert(armed_calls == 0);
}

/* A 256K game image streams through in 512-byte chunks with monotonic
 * progress; the gate that used to stop at 8K now stops at "no scheme". */
static void test_dbc_game_push(void)
{
    uint32_t off;
    uint8_t last_pct = 0;

    fresh();
    dbc_open(0, ASTROMAP_GAME256_SIZE);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_XFER);
    for (off = 0; off < ASTROMAP_GAME256_SIZE; off += 512) {
        uint8_t chunk[512];
        unsigned i;

        for (i = 0; i < sizeof chunk; i++)
            chunk[i] = (uint8_t)((off >> 12) + i);
        dbc_frame(CMD_NET_WRITE, chunk, sizeof chunk);
        assert(window[FN_R_BOOT_PCT] >= last_pct);
        last_pct = window[FN_R_BOOT_PCT];
    }
    dbc_frame(CMD_NET_CLOSE, NULL, 0);
    assert(pushed.len == ASTROMAP_GAME256_SIZE);
    assert(store[0] == 0 && store[ASTROMAP_GAME256_SIZE - 1]
           == (uint8_t)(((ASTROMAP_GAME256_SIZE - 512) >> 12) + 511));
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_READY);
    assert(window[FN_R_BOOT_PCT] == 100);
    reg_write(FN_REG_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    assert(armed_calls == 1);
}

/* F2: a commit-time mapping failure must never read as READY, and BOOTLOCK
 * must stay refused. (An app-shaped size passes the OPEN gate; only the
 * claim check at close can fail it.) */
static void test_dbc_close_nomap(void)
{
    static uint8_t image[ASTROMAP_WINDOW + FN_APP_PAGE_SIZE];

    fresh();
    pushed.close_err = FN_BOOT_ERR_NOMAP;
    memset(image, 0x5A, sizeof image);
    dbc_push(image, sizeof image);
    assert(pushed.closes == 1 && !pushed.aborted);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_FAILED);
    assert(window[FN_R_BOOT_ERR] == FN_BOOT_ERR_NOMAP);
    reg_write(FN_REG_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    assert(armed_calls == 0);
}

/* The .cfg sibling (stream 1, pushed before the ROM) must never touch the
 * boot state -- neither before the ROM push nor after READY. */
static void test_dbc_stream1_isolation(void)
{
    static uint8_t image[ASTROMAP_WINDOW];
    static const uint8_t cfg[64] = { 'c', 'f', 'g' };

    fresh();
    dbc_open(1, sizeof cfg);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_IDLE);
    dbc_frame(CMD_NET_WRITE, cfg, sizeof cfg);
    assert(window[FN_R_BOOT_PCT] == 0);
    dbc_frame(CMD_NET_CLOSE, NULL, 0);
    assert(pushed.stream == 1);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_IDLE);

    memset(image, 0xC3, sizeof image);
    dbc_push(image, sizeof image);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_READY);

    /* Out-of-order sibling after READY: still isolated. */
    dbc_open(1, sizeof cfg);
    dbc_frame(CMD_NET_WRITE, cfg, sizeof cfg);
    dbc_frame(CMD_NET_CLOSE, NULL, 0);
    assert(window[FN_R_BOOT_STATE] == FN_BOOT_READY);
    assert(window[FN_R_BOOT_PCT] == 100);
    reg_write(FN_REG_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    assert(armed_calls == 1);
}

static void test_error_path(void)
{
    fresh();
    reply_status = FB_ETIMEOUT;
    run_txn(1, 0x70, 0xC4);
    assert(window[FN_R_ERR] == FN_ERR_TIMEOUT);
    assert(window[FN_R_RXLEN_LO] == 0 && window[FN_R_RXLEN_HI] == 0);
    assert(window[FN_R_ACKSEQ] == 1);   /* errors are acknowledged too */
}

int main(void)
{
    test_paint();
    test_transaction();
    test_seq_gates();
    test_slice_echo();
    test_unpaired_regdata();
    test_refresh_sweeps();
    test_bootsel_pairing();
    test_dbc_push_and_bootlock();
    test_dbc_abort_and_toobig();
    test_dbc_game_push();
    test_dbc_close_nomap();
    test_dbc_stream1_isolation();
    test_error_path();
    printf("test_fujimail: all tests passed\n");
    return 0;
}
