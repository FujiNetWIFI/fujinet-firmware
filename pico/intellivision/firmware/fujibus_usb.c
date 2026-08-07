#include "fujibus_usb.h"
#include "tusb.h"
#include "pico/time.h"

fb_status_t fujibus_transact(uint8_t device, uint8_t command,
                              const fb_param_t *params, unsigned nparams,
                              const uint8_t *payload, uint16_t payload_len,
                              uint8_t *rx_buf, size_t rx_cap,
                              fb_reply_t *reply, uint32_t timeout_ms)
{
    if (!tud_cdc_connected())
        return FB_ENOLINK;

    // One desync away from resyncing onto a stale reply forever -- drop
    // anything sitting in the RX FIFO before starting a new transaction.
    while (tud_cdc_available()) {
        uint8_t junk[64];
        tud_cdc_read(junk, sizeof(junk));
        tud_task();
    }

    uint8_t txbuf[384];
    size_t txlen = fujibus_build_request(device, command, params, nparams,
                                          payload, payload_len,
                                          txbuf, sizeof(txbuf));
    if (!txlen)
        return FB_ETOOBIG;

    // tud_cdc_write()'s return is capped by CFG_TUD_CDC_TX_BUFSIZE; ignoring
    // it silently truncates the frame, so keep pumping until every byte is
    // actually queued.
    size_t sent = 0;
    while (sent < txlen) {
        uint32_t w = tud_cdc_write(&txbuf[sent], (uint32_t)(txlen - sent));
        sent += w;
        tud_cdc_write_flush();
        tud_task();
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
            if (b == 0xC0 && ++seen_end == 2)
                goto got_frame;
        }
    }
    return FB_ETIMEOUT;

got_frame:
    if (!fujibus_parse_reply(rx_buf, rxlen, reply))
        return FB_EBADFRAME;
    return FB_OK;
}
