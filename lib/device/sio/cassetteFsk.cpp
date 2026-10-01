#ifdef ESP_PLATFORM
#include "cassetteFsk.h"
#include "../../media/atari/casFsk.h"

#include <cstdio>
#include <memory>

#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <soc/uart_periph.h>

#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "bus.h"

// RMT clock for FSK playback: 1 MHz, so 1 tick == 1 us == fsk_next_portion's unit.
#define CASSETTE_FSK_RMT_RESOLUTION_HZ 1000000

namespace
{

// FskReadFn adapter over an fnFile*, for casFsk.h::fsk_scan_run.
size_t cassette_fsk_file_read(void *ctx, size_t offset, uint8_t *dst, size_t n)
{
    fnFile *file = static_cast<fnFile *>(ctx);
    if (fnio::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
        return 0;
    return fnio::fread(dst, 1, n, file);
}

struct HeapCapsFree
{
    void operator()(uint8_t *p) const
    {
        if (p)
            heap_caps_free(p);
    }
};
using PsramBuffer = std::unique_ptr<uint8_t[], HeapCapsFree>;

// Per-transmission encoder progress: lives on cassette_fsk_play_run()'s
// stack for the duration of one rmt_transmit(). Touched only by the ISR
// refill callback below while the transmission is in flight.
struct CassetteFskEncodeState
{
    size_t   value_index     = 0;
    uint32_t remaining_ticks = 0;
    bool     level_high      = false;
};

// Pulls the next RMT duration/level portion from `payload`. Skips
// zero-duration values (they still advance parity) without emitting a
// portion for them. Returns false once every value has been consumed.
bool IRAM_ATTR cassette_fsk_pull_portion(CassetteFskEncodeState &st, const uint8_t *payload,
                                          size_t value_count, uint32_t &out_ticks, bool &out_level)
{
    while (st.remaining_ticks == 0)
    {
        if (st.value_index >= value_count)
            return false;
        const uint16_t value = fsk_decode_le16(payload + st.value_index * 2);
        st.remaining_ticks = fsk_ticks_for_value(value);
        st.level_high = fsk_level_for_index(st.value_index);
        st.value_index++;
    }
    out_ticks = fsk_next_portion(st.remaining_ticks);
    st.remaining_ticks -= out_ticks;
    out_level = st.level_high;
    return true;
}

// Simple-encoder callback: generates RMT symbols on the fly from the
// preloaded payload (`data`/`data_size`, as handed to rmt_transmit()).
// Runs in ISR context (RMT ping-pong refill) — no file I/O, no malloc/free,
// no logging, no locks, no virtual dispatch.
size_t IRAM_ATTR cassette_fsk_encode_cb(const void *data, size_t data_size,
                                         size_t /*symbols_written*/, size_t symbols_free,
                                         rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    CassetteFskEncodeState *st = static_cast<CassetteFskEncodeState *>(arg);
    const uint8_t *payload = static_cast<const uint8_t *>(data);
    const size_t value_count = data_size / 2;
    size_t num = 0;

    while (num < symbols_free)
    {
        uint32_t d0 = 0, d1 = 0;
        bool l0 = false, l1 = false;

        if (!cassette_fsk_pull_portion(*st, payload, value_count, d0, l0))
        {
            *done = true;
            break;
        }
        if (!cassette_fsk_pull_portion(*st, payload, value_count, d1, l1))
        {
            // Odd tail: one portion left. duration1 == 0 ends the symbol
            // there without adding a spurious extra edge (same level held).
            symbols[num].duration0 = static_cast<uint16_t>(d0);
            symbols[num].level0 = l0;
            symbols[num].duration1 = 0;
            symbols[num].level1 = l0;
            num++;
            *done = true;
            break;
        }

        symbols[num].duration0 = static_cast<uint16_t>(d0);
        symbols[num].level0 = l0;
        symbols[num].duration1 = static_cast<uint16_t>(d1);
        symbols[num].level1 = l1;
        num++;
    }
    return num;
}

// Detaches UART2 TX from its GPIO and hands the pin to RMT — same pattern
// Turbo 2000 already uses on this pin (see turbo2000_init_rmt()).
void cassette_fsk_take_pin()
{
    SYSTEM_BUS.flushOutput();
    esp_rom_gpio_connect_out_signal(PIN_UART2_TX, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_level((gpio_num_t)PIN_UART2_TX, 1);
}

// Reattaches UART2 TX to its GPIO — the mirror image of cassette_fsk_take_pin().
void cassette_fsk_release_pin()
{
    esp_rom_gpio_connect_out_signal(PIN_UART2_TX,
        uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal, false, false);
}

} // namespace

CassetteFskPlayResult cassette_fsk_play_run(fnFile *file, size_t filesize, size_t header_offset,
                                             CassetteFskMotorDroppedFn motor_dropped, void *motor_ctx)
{
    CassetteFskPlayResult result;

    FskRunInfo run;
    if (!fsk_scan_run(cassette_fsk_file_read, file, filesize, header_offset, run))
    {
        result.status = CassetteFskStatus::malformed;
        return result;
    }

    PsramBuffer payload(static_cast<uint8_t *>(
        heap_caps_malloc(run.payload_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (!payload)
    {
        Debug_printf("FSK: PSRAM allocation of %u bytes failed (free SPIRAM: %u bytes)\n",
                     (unsigned)run.payload_bytes,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        result.status = CassetteFskStatus::allocation_failed;
        return result;
    }

    // Read each joined chunk's payload directly into its final position in
    // the buffer -- no scratch-buffer-then-copy step, so there is exactly
    // one copy of the run's bytes in memory at any time.
    size_t offset = header_offset;
    size_t written = 0;
    for (size_t i = 0; i < run.chunk_count; i++)
    {
        uint8_t hdr8[8];
        if (fnio::fseek(file, static_cast<long>(offset), SEEK_SET) != 0 ||
            fnio::fread(hdr8, 1, sizeof(hdr8), file) != sizeof(hdr8))
        {
            result.status = CassetteFskStatus::read_failed;
            return result;
        }
        const uint16_t chunk_len = fsk_decode_le16(hdr8 + 4);
        if (written + chunk_len > run.payload_bytes)
        {
            // Defensive only: fsk_scan_run already sized the buffer from
            // these same chunks. This re-read disagreeing with that would
            // mean the backing file changed under us -- refuse to write
            // past the buffer rather than trust it.
            result.status = CassetteFskStatus::read_failed;
            return result;
        }
        if (chunk_len > 0 &&
            fnio::fread(payload.get() + written, 1, chunk_len, file) != chunk_len)
        {
            result.status = CassetteFskStatus::read_failed;
            return result;
        }
        written += chunk_len;
        offset += sizeof(hdr8) + chunk_len;
    }
    Debug_printf("FSK: preloaded %u bytes (%u values) at offset %u, free SPIRAM now %u bytes\n",
                 (unsigned)run.payload_bytes, (unsigned)run.value_count, (unsigned)header_offset,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    const uint64_t waveform_ticks = fsk_sum_waveform_ticks(payload.get(), run.value_count);
    const uint64_t waveform_ms64 = waveform_ticks / 1000; // 1 tick == 1 us
    result.waveform_ms = waveform_ms64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(waveform_ms64);

    // ---- RMT: one-shot channel for this run only, torn down before return ----
    cassette_fsk_take_pin();

    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = (gpio_num_t)PIN_UART2_TX;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = CASSETTE_FSK_RMT_RESOLUTION_HZ;
    tx_cfg.mem_block_symbols = 64 * 8;
    tx_cfg.trans_queue_depth = 1;
    tx_cfg.intr_priority = 0;
    tx_cfg.flags.invert_out = false;
    tx_cfg.flags.with_dma = false;
    tx_cfg.flags.io_loop_back = false;
    tx_cfg.flags.io_od_mode = false;
    tx_cfg.flags.allow_pd = false;

    rmt_channel_handle_t channel = nullptr;
    if (rmt_new_tx_channel(&tx_cfg, &channel) != ESP_OK)
    {
        Debug_println("FSK: rmt_new_tx_channel failed");
        cassette_fsk_release_pin();
        result.status = CassetteFskStatus::allocation_failed;
        return result;
    }

    CassetteFskEncodeState encode_state;
    rmt_simple_encoder_config_t simple_cfg = {};
    simple_cfg.callback = cassette_fsk_encode_cb;
    simple_cfg.arg = &encode_state;
    simple_cfg.min_chunk_size = 0;

    rmt_encoder_handle_t encoder = nullptr;
    if (rmt_new_simple_encoder(&simple_cfg, &encoder) != ESP_OK)
    {
        Debug_println("FSK: rmt_new_simple_encoder failed");
        rmt_del_channel(channel);
        cassette_fsk_release_pin();
        result.status = CassetteFskStatus::allocation_failed;
        return result;
    }

    ESP_ERROR_CHECK(rmt_enable(channel));

    rmt_transmit_config_t tx_transmit_cfg = {};
    tx_transmit_cfg.loop_count = 0;
    tx_transmit_cfg.flags.eot_level = 0;
    tx_transmit_cfg.flags.queue_nonblocking = false;

    const esp_err_t tx_err = rmt_transmit(channel, encoder, payload.get(), run.payload_bytes,
                                           &tx_transmit_cfg);

    bool dropped = false;
    if (tx_err == ESP_OK)
    {
        while (rmt_tx_wait_all_done(channel, 100) == ESP_ERR_TIMEOUT)
        {
            if (motor_dropped(motor_ctx))
            {
                dropped = true;
                break;
            }
        }
    }
    else
    {
        Debug_printf("FSK: rmt_transmit error: %s\n", esp_err_to_name(tx_err));
    }

    rmt_disable(channel);
    rmt_del_channel(channel);
    rmt_del_encoder(encoder);
    cassette_fsk_release_pin();

    if (tx_err != ESP_OK)
    {
        result.status = CassetteFskStatus::transmit_failed;
        return result;
    }
    if (dropped)
    {
        result.status = CassetteFskStatus::motor_dropped;
        return result;
    }

    result.status = CassetteFskStatus::ok;
    result.next_offset = run.next_offset;
    return result;
}

#endif // ESP_PLATFORM
