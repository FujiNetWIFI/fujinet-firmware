#ifndef CASSETTE_H
#define CASSETTE_H

#include "../../include/pinmap.h"

#ifdef ESP_PLATFORM
#include <driver/rmt_types.h>
#include <esp_heap_caps.h> // ESP-only: FSK pointer table in internal 8-bit DRAM; payload blocks in 8-bit PSRAM
#include <esp_timer.h>     // esp_timer_get_time() — Active FSK Rewind physical-position clock interpolation
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h> // SemaphoreHandle_t for _cassette_lock / _fsk_channel_lock
#include "fsk_plan.h"      // FSK_RUN_MAX_CHUNKS for the run descriptors below
#endif

#include "bus.h"
#include "fnSystem.h"
#include "fnio.h"
#include "cassette_time_plan.h" // CassetteWalkState, cas_walk_tape_time() — pure, host-testable

#define CASSETTE_BAUDRATE 600
#define BLOCK_LEN 128

#define STARTBIT 0
#define STOPBIT 9

enum class cassette_mode_t
{
    playback = 0,
    record
};

// software uart conops for cassette
// wait for falling edge and set fsk_clock
// find next falling edge and compute period
// check if period different than last (reset denoise counter)
// if not different, increment denoise counter if < denoise threshold
// when denoise counter == denoise threshold, set demod output

// if state counter == 0, check demod output for start bit edge (low voltage, logic high)
// if start bit edge, record time in baud_clock;
// wait 1/2 period and then read demod output (check it is start bit)
// wait 1 period and get (next) first bit, (shift received byte to right) store it in received_byte;
// increment state counter; go back and wait
// when all 8 bits received wait one more period and check for stop bit
// if not stop bit, throw a frame sync error
// if stop bit, store byte in buffer, reset some stuff,

class softUART
{
protected:
    uint64_t baud_clock;
    uint16_t baud = CASSETTE_BAUDRATE;             // bps
    uint32_t period = 1000000 / CASSETTE_BAUDRATE; // microseconds

    uint8_t demod_output;
    uint8_t denoise_counter;
    uint8_t denoise_threshold = 3;

    uint8_t received_byte;
    uint8_t state_counter;

    uint8_t buffer[256];
    uint8_t index_in = 0;
    uint8_t index_out = 0;

public:
    uint8_t available();
    void set_baud(uint16_t b);
    uint16_t get_baud() { return baud; };
    uint8_t read();
    int8_t service(uint8_t b);
};

class sioCassette : public virtualDevice
{
public:
#ifdef ESP_PLATFORM
    sioCassette();  // creates _cassette_lock once, for the lifetime of this (singleton) object
#endif

protected:
    // FileSystem *_FS = nullptr;
    fnFile *_file = nullptr;
    size_t filesize = 0;

    bool _mounted = false;                                    // indicates if a CAS or WAV file is open
    bool cassetteActive = false;                              // indicates if something....
    bool pulldown = true;                                     // indicates if we should use the motorline for control
    cassette_mode_t cassetteMode = cassette_mode_t::playback; // If we are in cassette mode or not

    // FSK demod (from Atari for writing CAS, e.g, from a CSAVE)
    uint64_t fsk_clock; // can count period width from atari because
    uint8_t last_value = 0;
    uint8_t last_output = 0;
    uint8_t denoise_counter = 0;
    const uint16_t period_space = 1000000 / 3995;
    const uint16_t period_mark = 1000000 / 5327;
    uint8_t decode_fsk();

    // helper function to read motor pin
    bool motor_line() { return SYSTEM_BUS.motor_asserted(); }

    // have to populate virtual functions to complete class
    void sio_status(const FujiSIOPacket &packet) override{}; // $53, 'S', Status
    void sio_process(const FujiSIOPacket &packet) override{};

    void open_cassette_file(FileSystem *filesystem);
    void close_cassette_file();

public:
    void umount_cassette_file();
    void mount_cassette_file(fnFile *f, size_t fz);

    void sio_enable_cassette();  // setup cassette
    void sio_disable_cassette(); // stop cassette
    void sio_handle_cassette();  // Handle incoming & outgoing data for cassette

    void rewind(); // rewind cassette to start (offset 0) — independent of the time walker
    bool rewind_seconds(uint32_t seconds); // rewind by N seconds; interrupts an actively-transmitting
                                            // FSK run (Active FSK Rewind) instead of blocking for it

    bool is_mounted() { return _mounted; };
    bool is_active() { return cassetteActive; };
    bool has_pulldown() { return pulldown; };
    bool get_buttons();
    void set_buttons(bool play_record);
    void set_pulldown(bool resistor);

private:
    // stuff from SDrive Arduino sketch
    size_t tape_offset = 0;
    struct tape_FUJI_hdr
    {
        uint8_t chunk_type[4];
        uint16_t chunk_length;
        uint16_t irg_length;
        uint8_t data[];
    };

    struct t_flags
    {
        unsigned char FUJI : 1;
        unsigned char turbo : 1;
        unsigned char turbo2000 : 1;
        unsigned char qros : 1;
    } tape_flags;

    uint8_t atari_sector_buffer[256];

    void Clear_atari_sector_buffer(uint16_t len);

    unsigned short block;
    unsigned short baud;

    // ----------------------------------------------------------------------
    // Custom Rewind: real per-chunk time model (walker) + safe reposition
    // ----------------------------------------------------------------------
#ifdef ESP_PLATFORM
    // Guards tape_offset/_file/format-state against the HTTP task
    // (rewind()/rewind_seconds()) racing sio_handle_cassette(), which holds
    // this for its whole dispatch, including any in-flight
    // rmt_tx_wait_all_done(). No cancellation.
    SemaphoreHandle_t _cassette_lock = nullptr;
#endif

    // Walks chunk headers (or legacy blocks) from offset 0, accumulating
    // real playback duration and format state. Resolves either the CURRENT
    // position's elapsed time (stop_at_offset=tape_offset) or a TARGET time
    // back to a chunk-boundary offset (stop_at_offset=SIZE_MAX). Caller must
    // hold _cassette_lock. False only on a structural walk failure.
    bool walk_tape_time(size_t stop_at_offset, uint64_t stop_at_time_us,
                        CassetteWalkState &out) const;

    // Choke point before any tape_offset write: stops in-flight
    // FSK/Turbo2000/QROS waveforms and frees their buffers. Caller must hold
    // _cassette_lock. Idempotent.
    void stop_and_reset_for_reposition();

#ifdef ESP_PLATFORM
    // Cassette-task-side resolution of an Active FSK Rewind interrupt that
    // landed on the run starting at `run_chunk0_header_offset`. Called from
    // play_fsk_chunk(), holding _cassette_lock, after fsk_signal_end() flags
    // _fsk_interrupted_pending and before fsk_free_blocks() runs. Sets
    // _last_rewind_result, clears _fsk_rewind_state to IDLE, and returns the
    // offset play_fsk_chunk() should return as the new tape_offset.
    size_t fsk_resolve_active_rewind(size_t run_chunk0_header_offset,
                                     uint32_t seconds, int64_t stop_timestamp_us);

    // fsk_run_value_fn adapter over the resident block table for
    // cas_fsk_resolve_active_rewind() (cassette_time_plan.h). `ctx` is
    // `this`; reads only resident PSRAM, no file I/O.
    static uint16_t fsk_resident_run_value_reader(void *ctx, size_t chunk_index,
                                                  size_t value_index);
#endif

    size_t send_tape_block(size_t offset);
    void check_for_FUJI_file();
    size_t send_FUJI_tape_block(size_t offset);
    size_t receive_FUJI_tape_block(size_t offset);

    // QROS turbo cassette support
    bool qros_boot_sent = false;       // boot loader already sent?
    uint16_t qros_turbo_baud = 6580;   // turbo baud rate from CAS
    size_t send_QROS_tape_block(size_t offset);
    void send_QROS_boot_loader();
#ifdef ESP_PLATFORM
    void qros_pilot_on();   // detach UART TX, set GPIO HIGH for pilot tone
    void qros_pilot_off();  // reattach UART TX
    bool _qros_pilot_active = false;
#endif

    // Turbo 2000 PWM cassette support
    uint16_t t2k_pilot_half  = 726;   // pilot pulse half-period in µs
    uint16_t t2k_bit0_half   = 272;   // narrow pulse (bit 0) half-period in µs
    uint16_t t2k_bit1_half   = 589;   // wide pulse (bit 1) half-period in µs
    uint16_t t2k_pilot_count = 3072;  // pilot pulses before current block
    uint16_t t2k_samplerate  = 44100; // CAS file sample rate
    bool     t2k_msb_first   = true;  // bit order
    bool     t2k_boot_sent   = false; // boot loader already sent?
    size_t   t2k_data_present = 0;   // bytes already sent from pwmd by pwml pre-send

    size_t send_turbo2000_tape_block(size_t offset);
    void mount_turbo_loader();
    void unmount_turbo_loader();
    int8_t _turbo_loader_slot = -1;
    uint16_t t2k_samples_to_us(uint8_t samples);
#ifdef ESP_PLATFORM
    // Simple encoder callback needs access to our members
    friend size_t t2k_encode_cb(const void *, size_t, size_t, size_t,
                                rmt_symbol_word_t *, bool *, void *);

    void turbo2000_init_rmt();
    void turbo2000_deinit_rmt();
    void turbo2000_send_pulses(uint16_t half_period_us, int count);
    void turbo2000_send_byte(uint8_t byte);
    void turbo2000_send_bytes(const uint8_t *data, size_t length);
    void turbo2000_send_pilot(uint16_t count);
    void turbo2000_flush_rmt();
    void turbo2000_free_pending_buf(); // frees + nulls _t2k_pending_buf; idempotent
    void *_rmt_channel = nullptr;        // rmt_channel_handle_t
    void *_rmt_copy_encoder = nullptr;   // copy encoder for pilot tone
    void *_rmt_simple_encoder = nullptr; // simple encoder for data (gapless)
    bool _rmt_active = false;
    void *_t2k_pending_buf = nullptr;    // raw byte data buffer (freed after flush)

    // State for simple encoder callback (set before rmt_transmit)
    rmt_symbol_word_t _t2k_sync_syms[16];
    size_t _t2k_sync_count = 0;
    size_t _t2k_pilot_pending = 0; // pilot symbols to generate before sync+data
#endif

    // ----------------------------------------------------------------------
    // A8CAS raw FSK ("fsk ") chunk playback (segmented whole-payload preload)
    // ----------------------------------------------------------------------

    // FSK chunk playback (A8CAS "fsk " chunks), cross-platform entry point.
    // Preloads the whole clamped payload into a block table, honors the IRG,
    // reproduces the signal via the RMT stateful simple encoder (ESP) or
    // safely skips (PC), and returns the next read offset. Never changes baud.
    size_t play_fsk_chunk(size_t offset, uint16_t chunk_length, uint16_t irg_ms);

#ifdef ESP_PLATFORM
    // Segmented preload block size: small, and <= the TNFS per-read limit so
    // one fnio::fread fills it. 65535-byte max payload -> <= 128 blocks.
    static constexpr size_t FSK_PRELOAD_BLOCK_BYTES = 512;

    // Max bytes requested per preload read; stays under the TNFS 525-byte
    // per-read limit. Positive short reads are accumulated.
    static constexpr size_t FSK_PRELOAD_READ_MAX = 512;

    // Raw FSK signal helpers on the ESP RMT peripheral (same detach/reattach
    // pattern as Turbo 2000). resume_value_index seeds the ISR cursor at that
    // value index of run chunk 0 for an Active FSK Rewind resume (normally 0
    // = fresh start); fsk_signal_begin() never reads the shared
    // _fsk_resume_pending member itself, only this parameter.
    bool fsk_signal_begin(size_t resume_value_index = 0);   // alloc RMT channel + simple encoder, detach UART TX; false on failure
    void fsk_signal_emit();    // ONE rmt_transmit using the pointer table as payload; then wait-done
    void fsk_signal_end();     // idempotent teardown; also the serialization point that decides whether
                               // a rewind request claimed this channel — see cassette.cpp

    void fsk_free_blocks();    // free every preloaded block + the pointer table; idempotent

    // Preloads a contiguous zero-IRG FSK run into ONE PSRAM block table, each
    // chunk block-aligned, filling the run descriptors (_fsk_run_*). On any
    // failure frees everything and returns false (no partial waveform).
    bool fsk_preload_run(const size_t *run_offsets, const size_t *run_data_avail,
                         size_t count);

    // Stateful RMT simple-encoder callback. Generates symbols on demand from
    // the resident block table + the O(1) cursor below. No file I/O, no
    // allocation; never returns 0 to wait for data (min_chunk_size = 1).
    static size_t IRAM_ATTR fsk_encode_cb(const void *data, size_t data_size,
                                          size_t symbols_written, size_t symbols_free,
                                          rmt_symbol_word_t *symbols, bool *done, void *arg);

    void       *_fsk_rmt_channel = nullptr;
    void       *_fsk_rmt_encoder = nullptr;
    bool        _fsk_signal_active = false;

    // Preloaded payload as a block table: fully resident + immutable during
    // the transaction, freed only after rmt_tx_wait_all_done.
    uint8_t  **_fsk_blocks          = nullptr; // pointer table: _fsk_block_count entries
    size_t     _fsk_block_size      = 0;       // bytes per block (final block may be partly used)
    size_t     _fsk_block_count     = 0;       // number of blocks allocated
    size_t     _fsk_payload_len     = 0;       // total clamped logical payload bytes (0..65535)

    // O(1) ISR-only encoder cursor over the block table (set before
    // rmt_transmit; advanced only by fsk_encode_cb).
    size_t   _fsk_value_count       = 0;       // floor(_fsk_payload_len / 2); done when index reaches this
    size_t   _fsk_value_index       = 0;       // current original FSK value index (for parity)
    size_t   _fsk_payload_pos       = 0;       // logical payload byte position consumed by the encoder (== value_index*2)
    uint32_t _fsk_remaining_ticks   = 0;       // ticks left for the value being split (15-bit carry)
    bool     _fsk_level_high        = false;   // logical level of the value being split (index parity)

    // Contiguous zero-IRG FSK RUN descriptors: a run is a maximal sequence of
    // `fsk ` chunks where only the first carries a non-zero IRG. Preloaded
    // into ONE PSRAM block table, played as ONE RMT lifecycle so no gap is
    // inserted at container boundaries. Each chunk starts on its own block
    // boundary so per-chunk index parity resets cleanly.
    size_t   _fsk_run_chunk_count   = 0;       // number of chunks in the current run (>=1)
    size_t   _fsk_run_value_counts[FSK_RUN_MAX_CHUNKS] = {}; // per-chunk value_count
    size_t   _fsk_run_block_base[FSK_RUN_MAX_CHUNKS]   = {}; // per-chunk first block index
    // Encoder-cursor position within the run (ISR-only, set before rmt_transmit):
    size_t   _fsk_run_chunk_index   = 0;       // which run chunk the cursor is in

    // ----------------------------------------------------------------------
    // Active FSK Rewind: interrupt + precise-resume extension. Only the
    // "fsk " path is affected; other formats never publish
    // _fsk_active_channel and always take the original rewind_seconds() path.
    // ----------------------------------------------------------------------

    // Each chunk's payload START file offset (header offset is this minus 8),
    // needed to translate a resolved (chunk_index, value_index) back into a
    // real tape_offset without a TNFS re-read.
    size_t   _fsk_run_file_offsets[FSK_RUN_MAX_CHUNKS] = {};
    // This run's own leading IRG (only chunk 0's matters — every joined
    // chunk after it has IRG==0 by contract). Set once per run, consumed by
    // fsk_resolve_active_rewind().
    uint16_t _fsk_run_leading_irg_ms = 0;

    // Guards _fsk_active_channel and the request/result handshake. Separate
    // from, and much more briefly held than, _cassette_lock: HTTP takes only
    // this lock to interrupt a live transmission, without waiting for the
    // whole FSK run to finish.
    SemaphoreHandle_t _fsk_channel_lock = nullptr;

    // Published (under _fsk_channel_lock) only after rmt_transmit() has
    // returned ESP_OK — never merely after rmt_enable() — so any channel
    // HTTP observes here is guaranteed already transmitting. Unpublished
    // only inside fsk_signal_end(). Holds an rmt_channel_handle_t.
    void *_fsk_active_channel = nullptr;

    enum class FskRewindReq : uint8_t { IDLE, REQUESTED, PROCESSING };
    // One active rewind request maximum; guarded by _fsk_channel_lock.
    FskRewindReq _fsk_rewind_state = FskRewindReq::IDLE;
    uint32_t     _fsk_rewind_seconds_req = 0;        // guarded by _fsk_channel_lock
    int64_t      _fsk_rewind_stop_timestamp_us = 0;  // guarded by _fsk_channel_lock; esp_timer_get_time()
                                                      // captured by HTTP immediately BEFORE rmt_disable()

    enum class RewindResult : uint8_t { NONE, SUCCESS, FAILED, NATURAL_COMPLETION_FALLBACK, BUSY };
    // Set by the cassette task, guarded by _cassette_lock; HTTP's take() of
    // _cassette_lock after issuing the request is the happens-before edge
    // that makes reading this safe without polling.
    RewindResult _last_rewind_result = RewindResult::NONE;

    // Scratch handoff (task-local, no locking needed) from
    // fsk_signal_end()'s serialization point to play_fsk_chunk(): true iff a
    // rewind request genuinely claimed THIS transmission's channel.
    bool     _fsk_interrupted_pending = false;
    uint32_t _fsk_interrupted_seconds = 0;
    int64_t  _fsk_interrupted_stop_us = 0;

    // Physical progress (functional bookkeeping, no logging/diagnostics).
    // Written by the ISR, read by the cassette task only after
    // rmt_disable() returns (its busy-poll on real TX_DONE guarantees the
    // ISR is no longer running) — plain reads/writes, no additional lock.
    uint64_t _fsk_cumulative_ticks        = 0; // total ticks encoded into the run so far (monotonic, ISR-only)
    uint64_t _fsk_prefill_half_ticks      = 0; // cumulative_ticks at the exact first-256-symbol boundary during
                                                // prefill — never the prefill total, which can cover up to 512
                                                // symbols (two threshold halves) while only the first is proven
    uint64_t _fsk_pending_boundary_ticks  = 0; // cumulative_ticks as of the last confirmed 256-symbol boundary;
                                                // credited to _fsk_confirmed_ticks on the next real threshold refill
    uint64_t _fsk_confirmed_ticks         = 0; // last ticks count PROVEN physically emitted by real hardware
    int64_t  _fsk_confirmed_timestamp_us  = 0; // esp_timer_get_time() at the _fsk_confirmed_ticks snapshot
    bool     _fsk_transmission_started    = false; // true only strictly AFTER rmt_transmit() returns ESP_OK; gates
                                                    // every ISR write above so prefill callbacks can never update it

    // One-shot resume seed: set by fsk_resolve_active_rewind(), read +
    // cleared exactly once at the top of the NEXT play_fsk_chunk() call.
    bool   _fsk_resume_pending     = false;
    size_t _fsk_resume_value_index = 0;

#endif // ESP_PLATFORM
};

#endif
