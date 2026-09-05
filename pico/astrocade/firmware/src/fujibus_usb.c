/* fujibus_usb.c -- FujiBus transport over USB CDC to the ESP32-S3.
 *
 * The ESP32-S3 is the USB host and this cartridge is the device, so nothing
 * here enumerates anything; it reads and writes the CDC pipe TinyUSB gives us.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "fujibus_usb.h"

/* Sized for a SLIP-encoded DBC push frame, not for the mailbox: a 6-byte header
 * plus a 512-byte chunk is 518 decoded, and SLIP worst-case doubles that plus
 * two delimiters. Undersizing this truncates every ROM push, and both ends see
 * a timeout pointing nowhere near the cause. */
#define FUJIBUS_RAW_RX_MAX 1088
#define FUJIBUS_TX_MAX     640

static fujibus_inbound_fn inbound_handler;

void fujibus_set_inbound_handler(fujibus_inbound_fn handler)
{
    inbound_handler = handler;
}

bool fujibus_link_up(void)
{
    return tud_cdc_connected();
}

/* core1's bus loop shares the SRAM fabric with core0. Calling tud_task() with
 * no gap turns what would be a brief contention burst into continuous
 * contention for the whole wait. The Odyssey 2's ~1us PSEN window is far
 * roomier than the Intellivision's, where this was a hard failure, but the gap
 * costs nothing against a multi-second budget so it stays. */
static void pump(void)
{
    tud_task();
    busy_wait_us(500);
}

void fuji_wait_ms_pumped(uint32_t ms)
{
    absolute_time_t deadline = make_timeout_time_ms(ms);

    while (!time_reached(deadline))
        pump();
}

static bool write_all(const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    absolute_time_t deadline = make_timeout_time_ms(2000);

    while (sent < len) {
        uint32_t avail = tud_cdc_write_available();
        if (avail > 0) {
            size_t n = len - sent;
            if (n > avail)
                n = avail;
            sent += tud_cdc_write(buf + sent, n);
            tud_cdc_write_flush();
        }
        if (time_reached(deadline))
            return false;
        pump();
    }
    tud_cdc_write_flush();
    return true;
}

void fujibus_send_bare(uint8_t device, uint8_t command,
                       const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[64];
    size_t n = fujibus_build_request(device, command, NULL, 0,
                                     payload, payload_len, frame, sizeof frame);
    if (n)
        write_all(frame, n);
}

fb_status_t fujibus_transact(uint8_t device, uint8_t command,
                             const fb_param_t *params, unsigned nparams,
                             const uint8_t *payload, uint16_t payload_len,
                             uint32_t timeout_ms, fb_reply_t *reply)
{
    static uint8_t txbuf[FUJIBUS_TX_MAX];
    static uint8_t rxbuf[FUJIBUS_RAW_RX_MAX];
    size_t txlen, rxlen = 0;
    unsigned ends = 0;
    absolute_time_t deadline;

    if (!fujibus_link_up())
        return FB_ENOLINK;

    txlen = fujibus_build_request(device, command, params, nparams,
                                  payload, payload_len, txbuf, sizeof txbuf);
    if (txlen == 0)
        return FB_ETOOBIG;

    /* One desync and we resync onto a stale reply forever. */
    while (tud_cdc_available())
        tud_cdc_read_char();

    if (!write_all(txbuf, txlen))
        return FB_ENOLINK;

    deadline = make_timeout_time_ms(timeout_ms);
    for (;;) {
        while (tud_cdc_available()) {
            int c = tud_cdc_read_char();
            if (c < 0)
                break;
            if (rxlen >= sizeof rxbuf)
                return FB_ETOOBIG;
            rxbuf[rxlen++] = (uint8_t) c;
            if ((uint8_t) c != 0xC0 || ++ends < 2)
                continue;

            /* A complete frame. */
            if (!fujibus_parse_reply(rxbuf, rxlen, reply))
                return FB_EBADFRAME;
            if (inbound_handler != NULL && inbound_handler(reply)) {
                /* A push frame: it proves the link is alive rather than
                 * stalled, so the deadline restarts instead of counting down. */
                rxlen = 0;
                ends = 0;
                deadline = make_timeout_time_ms(timeout_ms);
                continue;
            }
            return FB_OK;
        }
        if (time_reached(deadline))
            return FB_ETIMEOUT;
        pump();
    }
}
