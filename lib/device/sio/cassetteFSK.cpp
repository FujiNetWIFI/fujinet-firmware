#if defined(ESP_PLATFORM) && defined(BUILD_ATARI)
#include "cassetteFSK.h"
#include "../../media/atari/casFSK.h"
#include "../../media/atari/casFSKLoader.h"

#include <cstdio>
#include <memory>

#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/uart_periph.h>

#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "bus.h"

// RMT clock for FSK playback: 1 MHz, so 1 tick == 1 us == fsk_next_portion's unit.
#define CASSETTE_FSK_RMT_RESOLUTION_HZ 1000000

// While the start gate waits, MOTOR is polled every this many RTOS ticks.
#define CASSETTE_FSK_GATE_MOTOR_POLL_TICKS 10

namespace
{

// FSKReadFn adapter over an fnFile*, for casFSK.h::fsk_scan_run.
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

// Per-transmission encoder state: lives on the caller's stack for the duration of one
// rmt_transmit(). Touched only by the ISR refill callback below while the transmission is in
// flight; `underrun` is read once it has ended.
struct CassetteFSKEncode
{
    FSKEncodeState     state;
    FSKPreloadedValues preloaded{nullptr, 0};
    FSKPublishedValues published{nullptr};
    bool               progressive = false;
    volatile bool      underrun = false;
};

// Simple-encoder callback: generates RMT symbols on the fly from the run's values (`data`/`data_size`,
// as handed to rmt_transmit(), are not used). Runs in ISR context (RMT ping-pong refill) — no file
// I/O, no malloc/free, no logging, no locks, no virtual dispatch.
size_t IRAM_ATTR cassette_fsk_encode_cb(const void * /*data*/, size_t /*data_size*/,
                                         size_t /*symbols_written*/, size_t symbols_free,
                                         rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    CassetteFSKEncode *enc = static_cast<CassetteFSKEncode *>(arg);
    FSKNext stop = FSKNext::end;
    const size_t num = enc->progressive
        ? fsk_fill_symbols(enc->state, enc->published, symbols, symbols_free, stop)
        : fsk_fill_symbols(enc->state, enc->preloaded, symbols, symbols_free, stop);
    if (stop == FSKNext::underrun)
        enc->underrun = true;
    *done = stop != FSKNext::value;
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

// One-shot RMT channel for this run only, torn down before return. `data`/`data_bytes` are the
// opaque payload rmt_transmit() requires; the encoder reads its values through `enc`.
CassetteFSKStatus cassette_fsk_transmit(CassetteFSKEncode &enc, const void *data, size_t data_bytes,
                                         CassetteFSKMotorDroppedFn motor_dropped, void *motor_ctx)
{
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
        return CassetteFSKStatus::allocation_failed;
    }

    rmt_simple_encoder_config_t simple_cfg = {};
    simple_cfg.callback = cassette_fsk_encode_cb;
    simple_cfg.arg = &enc;
    simple_cfg.min_chunk_size = 0;

    rmt_encoder_handle_t encoder = nullptr;
    if (rmt_new_simple_encoder(&simple_cfg, &encoder) != ESP_OK)
    {
        Debug_println("FSK: rmt_new_simple_encoder failed");
        rmt_del_channel(channel);
        cassette_fsk_release_pin();
        return CassetteFSKStatus::allocation_failed;
    }

    ESP_ERROR_CHECK(rmt_enable(channel));

    rmt_transmit_config_t tx_transmit_cfg = {};
    tx_transmit_cfg.loop_count = 0;
    tx_transmit_cfg.flags.eot_level = 0;
    tx_transmit_cfg.flags.queue_nonblocking = false;

    const esp_err_t tx_err = rmt_transmit(channel, encoder, data, data_bytes, &tx_transmit_cfg);

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
        return CassetteFSKStatus::transmit_failed;
    if (dropped)
        return CassetteFSKStatus::motor_dropped;
    if (enc.underrun)
        return CassetteFSKStatus::underrun;
    return CassetteFSKStatus::ok;
}

CassetteFSKPlayResult cassette_fsk_play_preloaded(fnFile *file, size_t filesize, size_t header_offset,
                                                   CassetteFSKMotorDroppedFn motor_dropped,
                                                   void *motor_ctx)
{
    CassetteFSKPlayResult result;

    FSKRunInfo run;
    if (!fsk_scan_run(cassette_fsk_file_read, file, filesize, header_offset, run))
    {
        result.status = CassetteFSKStatus::malformed;
        return result;
    }

    PsramBuffer payload(static_cast<uint8_t *>(
        heap_caps_malloc(run.payload_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (!payload)
    {
        Debug_printf("FSK: PSRAM allocation of %u bytes failed (free SPIRAM: %u bytes)\n",
                     (unsigned)run.payload_bytes,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        result.status = CassetteFSKStatus::allocation_failed;
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
            result.status = CassetteFSKStatus::read_failed;
            return result;
        }
        const uint16_t chunk_len = fsk_decode_le16(hdr8 + 4);
        if (written + chunk_len > run.payload_bytes)
        {
            // Defensive only: fsk_scan_run already sized the buffer from
            // these same chunks. This re-read disagreeing with that would
            // mean the backing file changed under us -- refuse to write
            // past the buffer rather than trust it.
            result.status = CassetteFSKStatus::read_failed;
            return result;
        }
        if (chunk_len > 0 &&
            fnio::fread(payload.get() + written, 1, chunk_len, file) != chunk_len)
        {
            result.status = CassetteFSKStatus::read_failed;
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

    CassetteFSKEncode enc;
    enc.preloaded = FSKPreloadedValues{payload.get(), run.value_count};
    result.status = cassette_fsk_transmit(enc, payload.get(), run.payload_bytes, motor_dropped, motor_ctx);
    if (result.status == CassetteFSKStatus::ok)
        result.next_offset = run.next_offset;
    return result;
}

enum class ProgressiveOutcome : uint8_t
{
    played,    // `result` says how the run went
    fall_back, // the loader could not serve this run: use the basic path
};

// Waits for the start gate, then plays the run while the loader keeps filling it.
ProgressiveOutcome cassette_fsk_stream_run(CassetteFSKLoader &loader,
                                           CassetteFSKMotorDroppedFn motor_dropped, void *motor_ctx,
                                           CassetteFSKPlayResult &result)
{
    const FSKProgressiveRun &run = loader.run();

    unsigned ticks = 0;
    for (;;)
    {
        const FSKStartGate gate = fsk_run_start_gate(run);
        if (gate == FSKStartGate::ready)
            break;
        if (gate == FSKStartGate::failed)
        {
            // The loader gave up while we waited: resume it from its cursor if it may be, keep waiting.
            if (fsk_run_state(run) == FSKLoaderState::failed &&
                loader.restart() == CassetteFSKLoaderRestart::restarted)
            {
                vTaskDelay(1);
                continue;
            }
            if (fsk_run_state(run) == FSKLoaderState::stopped)
            {
                result.status = CassetteFSKStatus::stopped;
                return ProgressiveOutcome::played;
            }
            Debug_println("FSK: loader failed before the runway, preloading");
            return ProgressiveOutcome::fall_back;
        }
        if (++ticks % CASSETTE_FSK_GATE_MOTOR_POLL_TICKS == 0 && motor_dropped(motor_ctx))
        {
            result.status = CassetteFSKStatus::motor_dropped;
            return ProgressiveOutcome::played;
        }
        vTaskDelay(1);
    }

    CassetteFSKEncode enc;
    enc.progressive = true;
    enc.published = FSKPublishedValues{&run};
    result.status = cassette_fsk_transmit(enc, run.blocks, run.block_capacity * sizeof(run.blocks[0]),
                                           motor_dropped, motor_ctx);
    if (result.status != CassetteFSKStatus::ok)
        return ProgressiveOutcome::played;

    // Finished by the encoder, which saw the run final: the producer's results are visible.
    if (fsk_run_state(run) != FSKLoaderState::final_run)
    {
        result.status = CassetteFSKStatus::underrun;
        return ProgressiveOutcome::played;
    }
    const uint64_t waveform_ms64 = run.total_ticks / 1000; // 1 tick == 1 us
    result.waveform_ms = waveform_ms64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(waveform_ms64);
    result.next_offset = run.next_offset;
    return ProgressiveOutcome::played;
}

} // namespace

CassetteFSKPlayResult cassette_fsk_play_run(fnFile *file, size_t filesize, size_t header_offset,
                                             uint16_t chunk_length,
                                             CassetteFSKMotorDroppedFn motor_dropped, void *motor_ctx,
                                             CassetteFSKLoader &loader)
{
    // The gap is not part of the structural check.
    const FSKChunkHeader first = {{'f', 's', 'k', ' '}, chunk_length, 0};
    if (fsk_check_chunk(first, header_offset, filesize) == FSKChunkCheck::ok &&
        fsk_progressive_wanted(chunk_length) &&
        loader.start(file, filesize, header_offset, chunk_length) == CassetteFSKLoaderStart::started)
    {
        CassetteFSKPlayResult result;
        const ProgressiveOutcome outcome = cassette_fsk_stream_run(loader, motor_dropped, motor_ctx, result);
        loader.release();
        if (outcome == ProgressiveOutcome::played)
            return result;
    }

    return cassette_fsk_play_preloaded(file, filesize, header_offset, motor_dropped, motor_ctx);
}

#endif // ESP_PLATFORM && BUILD_ATARI
