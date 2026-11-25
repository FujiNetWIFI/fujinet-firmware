// See m1_card.h.
#include "m1_card.h"

#include <string.h>

static void modem_rx(void *ctx, const uint8_t *buf, uint32_t len)
{
    m1_card_t *card = (m1_card_t *)ctx;
    for (uint32_t idx = 0; idx < len; idx++)
        ring8_put(&card->uart.inbound, buf[idx]); // full: the modem outran the CPU
}

// FLUSH or INFO: either way a stream in progress ends.
static void host_mark(void *ctx, uint16_t mark)
{
    m1_card_t *card = (m1_card_t *)ctx;
    card->info_left = mark == M1_MARK_INFO ? M1_INFO_LEN : 0;
}

static void host_stream(void *ctx, ring8_t *to_host)
{
    m1_card_t *card = (m1_card_t *)ctx;
    uint8_t info[M1_INFO_LEN] = {'M', '1', card->saved, card->forced_off, card->settings.enabled};
    while (card->info_left && ring8_put(to_host, info[M1_INFO_LEN - card->info_left]))
        card->info_left--;
}

static const fuji_iolink_ops_t m1_link_ops = {modem_rx, host_mark, host_stream};

// Only 256-byte single-density images, one side: JV1.
bool fuji_tcache_accept(const fuji_percom_t *percom, uint16_t *tracks, uint8_t *spt)
{
    if (percom->size != WD_SECTOR || percom->sides != 1 || percom->cylinders == 0 ||
        percom->spt == 0 || percom->spt > FUJI_TC_SPT_MAX)
        return false;
    *tracks = percom->cylinders;
    *spt = (uint8_t)percom->spt;
    return true;
}

void m1_card_init(m1_card_t *card, const m1_settings_t *settings, uint8_t forced_off,
                  fuji_tx_fn send, void *send_ctx, uint32_t now)
{
    memset(card, 0, sizeof(*card));
    card->settings = *settings;
    card->saved = settings->enabled;
    card->forced_off = forced_off;
    wd_init(&card->fdc);
    fuji_pipe_init(&card->pipe, now);
    m1_uart_init(&card->uart);
    ring8_init(&card->printer, card->printer_buf, M1_PRINTER_RING);
    fuji_tcache_init(&card->cache);
    card->rtc_next = now + M1_RTC_US;
    fuji_iolink_cfg_t cfg = {&card->pipe, &card->cache, &card->uart.outbound, &card->printer,
                             &m1_link_ops, card, send, send_ctx};
    fuji_iolink_init(&card->link, &cfg);
    m1_bus_reset(card); // power-on: the 1771 restores
}

void m1_card_force_off(m1_card_t *card, uint8_t forced_off)
{
    card->forced_off = forced_off;
    card->settings.enabled &= (uint8_t)~forced_off;
}

bool m1_card_take_save(m1_card_t *card)
{
    return __atomic_exchange_n(&card->save_due, 0, __ATOMIC_ACQ_REL) != 0;
}

void m1_card_power_on(m1_card_t *card)
{
    fb_param_t config_mode = {0, 1};
    fuji_iolink_card_request(&card->link, FUJI_DEVICEID_FUJINET, CMD_FUJI_SET_BOOT_MODE,
                             &config_mode);
}

void m1_card_service(m1_card_t *card, uint32_t now)
{
    wd_service(&card->fdc, &card->cache, now);
    if ((int32_t)(now - card->rtc_next) >= 0)
    {
        __atomic_fetch_add(&card->rtc_ticks, 1, __ATOMIC_RELEASE);
        card->rtc_next += M1_RTC_US;
        if ((int32_t)(now - card->rtc_next) >= 0)
            card->rtc_next = now + M1_RTC_US; // fell behind: no burst of ticks
    }
    m1_uart_service(&card->uart, now);
    fuji_iolink_poll(&card->link, now);
}
