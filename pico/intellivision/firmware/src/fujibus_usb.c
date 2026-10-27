#include "fujibus_usb.h"
#include "tusb.h"
#include "pico/time.h"

static fujibus_inbound_fn s_inbound_handler = NULL;

void fujibus_set_inbound_handler(fujibus_inbound_fn handler)
{
    s_inbound_handler = handler;
}

void fujibus_send_frame(const uint8_t *frame, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        sent += tud_cdc_write(&frame[sent], (uint32_t)(len - sent));
        tud_cdc_write_flush();
        tud_task();
    }
    tud_cdc_write_flush();
}

fb_status_t fujibus_transact(uint8_t device, uint8_t command,
                              const fb_param_t *params, unsigned nparams,
                              const uint8_t *payload, uint16_t payload_len,
                              uint8_t *rx_buf, size_t rx_cap,
                              fb_reply_t *reply, uint32_t timeout_ms)
{
    if (!tud_cdc_connected())
        return FB_ENOLINK;

    // Drain stale bytes so a desync can't pair us with an old reply.
    while (tud_cdc_available()) {
        uint8_t junk[64];
        tud_cdc_read(junk, sizeof(junk));
        tud_task();
    }

    uint8_t txbuf[640];
    size_t txlen = fujibus_build_request(device, command, params, nparams,
                                          payload, payload_len,
                                          txbuf, sizeof(txbuf));
    if (!txlen)
        return FB_ETOOBIG;

    // tud_cdc_write() queues at most CFG_TUD_CDC_TX_BUFSIZE per call
    size_t sent = 0;
    while (sent < txlen) {
        uint32_t w = tud_cdc_write(&txbuf[sent], (uint32_t)(txlen - sent));
        sent += w;
        tud_cdc_write_flush();
        tud_task();
        if (sent < txlen)
            busy_wait_us(500); // see rationale in the RX wait loop below
    }
    tud_cdc_write_flush();

    size_t rxlen = 0;
    unsigned seen_end = 0;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        tud_task();
        while (tud_cdc_available() && rxlen < rx_cap) {
            uint8_t b;
            tud_cdc_read(&b, 1);
            rx_buf[rxlen++] = b;
            if (b == 0xC0 && ++seen_end == 2) {
                // May be an inbound ROM-push frame instead of our reply;
                // if the handler takes it, the link is alive, so restart
                // the deadline.
                fb_reply_t maybe;
                if (!fujibus_parse_reply(rx_buf, rxlen, &maybe))
                    return FB_EBADFRAME;

                if (s_inbound_handler != NULL && s_inbound_handler(&maybe)) {
                    rxlen = 0;
                    seen_end = 0;
                    deadline = make_timeout_time_ms(timeout_ms);
                    break;
                }

                *reply = maybe;
                return FB_OK;
            }
        }
        // core1's bus loop has ~300 cycles of slack per bus phase and shares
        // SRAM with core0; back-to-back tud_task() calls would contend for
        // the whole wait. The gap gives core1 idle windows.
        busy_wait_us(500);
    }
    return FB_ETIMEOUT;
}
