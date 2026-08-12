#include "fujibus_usb.h"
#include "tusb.h"
#include "pico/time.h"

static fujibus_inbound_fn s_inbound_handler = NULL;

void fujibus_set_inbound_handler(fujibus_inbound_fn handler)
{
    s_inbound_handler = handler;
}

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

    uint8_t txbuf[640];
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
                // A complete frame -- but it might be an inbound request
                // (the ESP32 pushing a ROM to FUJI_DEVICEID_DBC mid-
                // transaction) rather than the reply we're actually
                // waiting for. Let the registered handler have first
                // look; if it consumes the frame, reset and keep waiting,
                // with a fresh deadline (the frame itself proves the link
                // is alive and busy, not stalled).
                fb_reply_t maybe;
                if (!fujibus_parse_reply(rx_buf, rxlen, &maybe))
                    return FB_EBADFRAME;

                if (s_inbound_handler != NULL && s_inbound_handler(&maybe)) {
                    rxlen = 0;
                    seen_end = 0;
                    deadline = make_timeout_time_ms(timeout_ms);
                    break; // out of the inner while; outer while re-checks time_reached
                }

                *reply = maybe;
                return FB_OK;
            }
        }
        // core1's CP-1610 bus loop shares the SRAM fabric with core0 and
        // runs with only ~300 clock cycles of slack per bus phase. Calling
        // tud_task() back-to-back with no gap turns what would otherwise be
        // a brief contention burst (e.g. enumeration) into continuous
        // contention for the full length of this wait -- worst case 5s.
        // Sustained contention is far likelier to catch core1 mid-phase at
        // least once than a one-shot burst is. A short gap here costs
        // nothing against a multi-second timeout budget but gives core1
        // real idle windows between polls.
        busy_wait_us(500);
    }
    return FB_ETIMEOUT;
}
