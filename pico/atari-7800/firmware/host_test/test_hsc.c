/* test_hsc.c -- the High Score Cart's RAM as 32 AppKeys (hsc.c), against a
 * model of the ESP32's AppKeyMixin: OPEN carries the 6-byte key, a WRITE
 * stores at most 64 bytes and clears the open key, a READ returns a 2-byte
 * length then the data (the rs232 framing), a missing key reads empty.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hsc.h"

static uint8_t keys[256][64];
static bool have[256];
static int open_key = -1, open_mode = -1;
static bool link = true, sd = true;
static uint32_t now;
static unsigned writes, fail_after = 1000;
static uint8_t reply[2 + 64];

static fb_status_t transact(uint8_t device, uint8_t command,
                            const fb_param_t *params, unsigned nparams,
                            const uint8_t *payload, uint16_t payload_len,
                            uint32_t timeout_ms, fb_reply_t *r)
{
    (void)params; (void)nparams; (void)timeout_ms;
    assert(device == FUJI_DEVICE_ID);
    r->device = device;
    r->data = reply;
    r->data_len = 0;
    r->command = CMD_FUJI_NAK;
    if (!sd)
        return FB_OK;
    switch (command) {
    case CMD_APPKEY_OPEN:
        assert(payload_len == 6);
        assert((payload[0] | payload[1] << 8) == HSC_CREATOR && payload[2] == HSC_APP);
        open_key = payload[3];
        open_mode = payload[4];
        r->command = CMD_FUJI_ACK;
        break;
    case CMD_APPKEY_WRITE:
        assert(open_mode == 1 && payload_len == HSC_CHUNK);
        if (writes++ >= fail_after)
            return FB_ETIMEOUT;
        memcpy(keys[open_key], payload, 64);
        have[open_key] = true;
        open_key = open_mode = -1;
        r->command = CMD_FUJI_ACK;
        break;
    case CMD_APPKEY_READ:
        assert(open_mode == 0);
        if (have[open_key]) {
            reply[0] = 64;
            reply[1] = 0;
            memcpy(reply + 2, keys[open_key], 64);
            r->data_len = 66;
        } else {
            reply[0] = reply[1] = 0;
            r->data_len = 2;
        }
        r->command = CMD_FUJI_ACK;
        break;
    default:
        assert(0);
    }
    return FB_OK;
}

static bool link_up(void) { return link; }
static uint32_t now_ms(void) { return now; }

static const hsc_port_t port = { transact, link_up, now_ms };

int main(void)
{
    static uint8_t ram[2048];
    volatile uint32_t dirty = 0;
    hsc_t h;
    unsigned k;

    printf("test_hsc\n");

    /* nothing saved yet: every chunk reads $FF */
    memset(ram, 0x00, sizeof ram);
    hsc_init(&h, &port, ram, &dirty);
    hsc_service(&h, true);
    assert(h.restored);
    for (k = 0; k < sizeof ram; k++)
        assert(ram[k] == 0xFF);

    /* a game writes two chunks; nothing goes out until 2 s after the last */
    ram[5] = 0x11;
    ram[64 * 7 + 3] = 0x22;
    dirty |= 1u << 0 | 1u << 7;
    now = 100;
    hsc_service(&h, true);
    assert(writes == 0);
    now = 100 + HSC_DEBOUNCE_MS - 1;
    hsc_service(&h, true);
    assert(writes == 0);
    now = 100 + HSC_DEBOUNCE_MS;
    hsc_service(&h, false);                     /* not idle: still nothing */
    assert(writes == 0);
    hsc_service(&h, true);
    assert(writes == 2 && dirty == 0 && h.pending == 0 && h.sd_ok);
    assert(keys[0][5] == 0x11 && keys[7][3] == 0x22);

    /* a failed save keeps the chunk and retries later */
    ram[64 * 31] = 0x33;
    dirty |= 1u << 31;
    now += 10;
    hsc_service(&h, true);
    now += HSC_DEBOUNCE_MS;
    fail_after = writes;                        /* the next write times out */
    hsc_service(&h, true);
    assert(h.pending == (1u << 31) && !h.sd_ok && !have[31]);
    fail_after = 1000;
    hsc_service(&h, true);                      /* too soon after the failure */
    assert(h.pending == (1u << 31));
    now += HSC_DEBOUNCE_MS;
    hsc_service(&h, true);
    assert(h.pending == 0 && keys[31][0] == 0x33);

    /* a power cycle: the RAM comes back from the keys */
    memset(ram, 0x00, sizeof ram);
    hsc_init(&h, &port, ram, &dirty);
    link = false;
    hsc_service(&h, true);
    assert(!h.restored);
    link = true;
    hsc_service(&h, true);
    assert(h.restored && ram[5] == 0x11 && ram[64 * 7 + 3] == 0x22 && ram[64 * 31] == 0x33);
    assert(ram[64 * 2] == 0xFF);                /* never written */

    /* no SD card: restore completes, says so, and leaves the RAM (the
     * flash copy) alone */
    sd = false;
    hsc_init(&h, &port, ram, &dirty);
    assert(hsc_restore(&h) && !h.sd_ok && ram[5] == 0x11 && ram[64 * 2] == 0xFF);

    printf("test_hsc: ok\n");
    return 0;
}
