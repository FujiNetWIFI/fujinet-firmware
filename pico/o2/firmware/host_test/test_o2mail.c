/* test_o2mail.c -- host tests for the mailbox service: how long each command
 * may take, what a MOUNT_IMAGE resets, and which push stream drives the percent.
 */

#include <stdio.h>
#include <string.h>

#include "fujimail.h"
#include "fuji_mailbox.h"

static int failures;

static void check(const char *what, int passed)
{
    printf("  %-62s %s\n", what, passed ? "ok" : "FAIL");
    if (!passed)
        failures++;
}

/* The page as the port saw it, and the address stored last. */
static uint8_t page[0x1000];
static unsigned last_poke;
static uint32_t last_timeout;
static unsigned transacts;
static uint8_t state_at_transact, pct_at_transact;

static void port_poke(unsigned addr, uint8_t value)
{
    page[addr] = value;
    last_poke = addr;
}

static bool port_link_up(void) { return true; }

static fb_status_t port_transact(uint8_t device, uint8_t command,
                                 const fb_param_t *params, unsigned nparams,
                                 const uint8_t *payload, uint16_t payload_len,
                                 uint32_t timeout_ms, fb_reply_t *reply)
{
    (void) device; (void) command; (void) params; (void) nparams;
    (void) payload; (void) payload_len;
    last_timeout = timeout_ms;
    transacts++;
    state_at_transact = page[FN_R_BOOT_STATE];
    pct_at_transact = page[FN_R_BOOT_PCT];
    reply->device = device;
    reply->command = CMD_FUJI_ACK;
    reply->data = NULL;
    reply->data_len = 0;
    return FB_OK;
}

static void port_send_bare(uint8_t device, uint8_t command,
                           const uint8_t *payload, uint16_t payload_len)
{
    (void) device; (void) command; (void) payload; (void) payload_len;
}

static void port_stream_end(int stream, const uint8_t *data, unsigned len, bool aborted)
{
    (void) stream; (void) data; (void) len; (void) aborted;
}

static const fujimail_port_t port = {
    .poke = port_poke,
    .link_up = port_link_up,
    .transact = port_transact,
    .send_bare = port_send_bare,
    .stream_end = port_stream_end,
};

static void reg(uint8_t regnum, uint8_t value)
{
    fujimail_write(FN_W_REGSEL, regnum);
    fujimail_write(FN_W_REGDATA, value);
}

static uint8_t seq;

/* One command with no parameters, as the console would send it. */
static void run(uint8_t command)
{
    reg(FN_REG_DATA_RST, 0);
    reg(FN_REG_DEVICE, 0x70);
    reg(FN_REG_CMD, command);
    reg(FN_REG_NPARAM, 0);
    reg(FN_REG_TXLEN_LO, 0);
    reg(FN_REG_TXLEN_HI, 0);
    last_poke = 0;
    fujimail_write(FN_W_SEQ, ++seq);
}

static void push(uint8_t stream, uint32_t size, const uint8_t *data, unsigned len)
{
    uint8_t open[5];
    fb_reply_t frame;

    open[0] = stream;
    open[1] = (uint8_t) size;
    open[2] = (uint8_t)(size >> 8);
    open[3] = (uint8_t)(size >> 16);
    open[4] = (uint8_t)(size >> 24);
    frame.device = FUJI_DEVICEID_DBC;
    frame.command = CMD_NET_OPEN;
    frame.data = open;
    frame.data_len = 5;
    fujimail_inbound(&frame);
    frame.command = CMD_NET_WRITE;
    frame.data = data;
    frame.data_len = (uint16_t) len;
    fujimail_inbound(&frame);
}

int main(void)
{
    static uint8_t chunk[256];

    fujimail_init(&port);

    printf("fujimail: how long a command may take\n");
    run(CMD_FUJI_GET_ADAPTERCONFIG_EXTENDED);
    check("an ordinary command waits 5 s", last_timeout == 5000);
    run(CMD_FUJI_SCAN_NETWORKS);
    check("a WiFi scan waits 20 s", last_timeout == 20000);
    run(CMD_FUJI_COPY_FILE);
    check("COPY_FILE waits 60 s", last_timeout == 60000);
    run(CMD_FUJI_MOUNT_IMAGE);
    check("MOUNT_IMAGE waits 60 s", last_timeout == 60000);

    printf("fujimail: a MOUNT_IMAGE starts from idle\n");
    page[FN_R_BOOT_STATE] = FN_BOOT_READY;
    page[FN_R_BOOT_PCT] = 100;
    page[FN_R_BOOT_ERR] = FN_BOOT_ERR_BUSY;
    run(CMD_FUJI_MOUNT_IMAGE);
    check("state, percent and error are reset before the push can begin",
          state_at_transact == FN_BOOT_IDLE && pct_at_transact == 0
          && page[FN_R_BOOT_ERR] == 0);
    check("ACKSEQ is stored last, after the reply", last_poke == FN_R_ACKSEQ);
    page[FN_R_BOOT_STATE] = FN_BOOT_READY;
    page[FN_R_BOOT_PCT] = 100;
    run(CMD_FUJI_GET_ADAPTERCONFIG_EXTENDED);
    check("another command leaves them alone",
          page[FN_R_BOOT_STATE] == FN_BOOT_READY && page[FN_R_BOOT_PCT] == 100);

    printf("fujimail: the percent follows the ROM stream\n");
    memset(chunk, 0, sizeof chunk);
    push(1, 1000, chunk, 100);
    check("a .cfg stream does not move it", page[FN_R_BOOT_PCT] == 0
          && page[FN_R_BOOT_STATE] == FN_BOOT_XFER);
    push(0, 1000, chunk, 250);
    check("the image stream does", page[FN_R_BOOT_PCT] == 25);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
