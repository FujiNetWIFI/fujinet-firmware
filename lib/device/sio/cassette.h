#ifndef CASSETTE_H
#define CASSETTE_H

#include "../../include/pinmap.h"

#ifdef ESP_PLATFORM
#include <driver/rmt_types.h>
#include <esp_heap_caps.h> // ESP-only: FSK pointer table in internal 8-bit DRAM; payload blocks in 8-bit PSRAM
#include <esp_timer.h>     // esp_timer_get_time() — MOTOR pause / IRG deadline clock and physical-position interpolation
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
    bool rewind_seconds(uint32_t seconds); // rewind by N seconds, relative to the frozen (or, while a raw-FSK
                                            // run is transmitting, freshly frozen) cassette position

    // Raw-FSK MOTOR pause support, called by systemBus::service():
    //   fsk_background()     - task-context cleanup of a muted, draining RMT transmission
    //   fsk_resume_blocked() - true while raw-FSK playback must not (re)start because old RMT
    //                          resources are still draining or an HTTP rewind is being applied
    //                          to a frozen FSK position (never true for other cassette formats)
    void fsk_background();
    bool fsk_resume_blocked() const;

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

    // Resolves an absolute target time to the run-relative (R', Q') position
    // (cassette_time_plan.h) over the shared _file, restoring the caller's
    // cursor on return. Caller must hold _cassette_lock.
    bool resolve_target_time(uint64_t target_us, CassetteTargetResolution &out) const;

#ifdef ESP_PLATFORM
    // fsk_run_value_fn adapter over the resident block table for
    // cas_fsk_locate_ticks() (cassette_time_plan.h). `ctx` is `this`; reads
    // only resident PSRAM, no file I/O.
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
    // pattern as Turbo 2000). The seed positions the ISR cursor inside the run
    // for a resume: (chunk, value) chunk-local, `skip_ticks` already consumed of
    // that value (0 = fresh start or an exact value boundary).
    bool fsk_signal_begin(size_t seed_chunk = 0, size_t seed_value = 0,
                          uint64_t seed_skip_ticks = 0); // alloc RMT channel + simple encoder, detach UART TX; false on failure

    enum class FskEmit : uint8_t { NOT_STARTED, NATURAL, FROZEN };
    // ONE rmt_transmit using the pointer table as payload, then an event-driven
    // wait: natural completion, or a MOTOR/HTTP claim that freezes the position,
    // routes DATA IN back to the UART (MARK) and leaves the muted RMT draining.
    // `wave_seed_ticks` is the waveform position the run starts at (0 = fresh).
    FskEmit fsk_signal_emit(uint64_t wave_seed_ticks);

    // Synchronous teardown of a COMPLETED (or never-started) transmission.
    // A no-op while the transaction is draining: fsk_cleanup_drained() owns it.
    void fsk_signal_end();

    // Task-context release of a drained transmission: delete channel/encoder,
    // restore UART routing, free blocks. Caller holds _cassette_lock.
    void fsk_cleanup_drained();

    // Claim/freeze helpers (see cassette.cpp).
    FskStopReason fsk_take_stop_reason();
    void fsk_motor_isr_arm();
    void fsk_motor_isr_disarm();
    static void IRAM_ATTR fsk_motor_isr(void *arg);
    static bool IRAM_ATTR fsk_tx_done_cb(rmt_channel_handle_t chan,
                                         const rmt_tx_done_event_data_t *edata, void *arg);

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
    // MOTOR-aware raw-FSK pause / resume + freeze-first rewind
    // ----------------------------------------------------------------------
    //
    // Position model (R, Q): R is tape_offset (the header of the run's first
    // chunk), Q (_fsk_pos_us) is microseconds from walker_time(R), i.e. from
    // BEFORE R's leading IRG. cas_run_pos_split(irg_us, Q) tells whether Q is
    // inside the IRG or the waveform. _fsk_pos_valid marks a frozen position
    // waiting to be resumed or rewound; it is written under _cassette_lock
    // (mount/umount only ever clear it).
    bool     _fsk_pos_valid = false;
    uint64_t _fsk_pos_us    = 0;

    // Set by play_fsk_chunk() when the dispatch stopped because of a freeze
    // (MOTOR OFF or an HTTP rewind claim); send_FUJI_tape_block() then returns
    // the run's offset to sio_handle_cassette() instead of walking on.
    bool _fsk_dispatch_interrupted = false;

    // Tape time base (fsk_timebase_* in fsk_plan.h): the tape has been running
    // since MOTOR ON, not since the waveform could start. _fsk_motor_on_us is when
    // this MOTOR ON was seen. _fsk_timebase_pending arms the FIRST dispatch after
    // it, and only at the physical start of the tape; send_FUJI_tape_block() moves
    // it into _fsk_timebase_walk for the one walk it starts, and play_fsk_chunk()
    // consumes that. Resumes and rewinds never see it set.
    int64_t _fsk_motor_on_us = 0;
    bool    _fsk_timebase_pending = false;
    bool    _fsk_timebase_walk = false;

    // Transmission lifecycle. DRAINING = position already frozen, DATA IN
    // already back on the UART (MARK), but the RMT channel, encoder and the
    // resident blocks are still owned by hardware that has not signalled done.
    enum class FskTxState : uint8_t { IDLE, PLAYING, DRAINING };
    volatile FskTxState _fsk_tx_state = FskTxState::IDLE;

    // ISR-visible flags. The cassette task raises the stop flag (the encoder
    // callback only reads it); the RMT done callback sets hw_done; the encoder
    // sets encoding_complete.
    volatile bool _fsk_stop_flag         = false; // soft stop: encoder returns done at its next refill
    volatile bool _fsk_hw_done           = false; // RMT on_trans_done fired
    volatile bool _fsk_encoding_complete = false; // every value was encoded (natural end reached)

    // Wake-up for the emit wait loop (RMT done, MOTOR edge, HTTP claim).
    SemaphoreHandle_t _fsk_evt_sem = nullptr;
    volatile bool     _fsk_motor_edge = false;
    volatile int64_t  _fsk_motor_edge_ts_us = 0;
    bool              _fsk_motor_isr_armed = false;

    // Guarded by _fsk_channel_lock: who owns the live transaction, and the
    // rewind an HTTP claimant still has to apply once the dispatch has returned.
    FskStopReason     _fsk_stop_reason = FskStopReason::NONE;
    volatile uint32_t _fsk_pending_rewind_s = 0;
    int64_t           _fsk_stop_ts_us = 0;

    int64_t  _fsk_drain_start_us    = 0; // esp_timer_get_time() when the drain began
    uint64_t _fsk_frozen_wave_ticks = 0; // fsk_signal_emit -> play_fsk_chunk handoff

    // Guards _fsk_active_channel and the stop-claim state. Separate from, and
    // much more briefly held than, _cassette_lock. Lock order: _cassette_lock
    // (outer), then _fsk_channel_lock (inner); HTTP never nests them the other
    // way round.
    SemaphoreHandle_t _fsk_channel_lock = nullptr;

    // Published (under _fsk_channel_lock) only after rmt_transmit() has
    // returned ESP_OK, unpublished by a claimant or by fsk_signal_end(). Holds
    // an rmt_channel_handle_t.
    void *_fsk_active_channel = nullptr;

    // Physical progress (functional bookkeeping, no logging/diagnostics).
    // Written by the encoder ISR until _fsk_stop_flag is set; the cassette task
    // snapshots it inside a critical section (same core as the RMT interrupt)
    // and sets the flag in that same section. The counters are seeded with the
    // waveform position the run starts at, so a resumed run measures from the
    // correct place.
    // Resident-run cache: a freeze keeps the preloaded run (block table + run
    // descriptors) so the next resume needs neither the header scan nor the SD
    // preload. Marked by play_fsk_chunk() at every freeze, cleared by any free of
    // the blocks and by mount / unmount / reposition.
    bool     _fsk_resident_valid    = false; // the resident run may be reused by a resume of R
    bool     _fsk_keep_resident     = false; // this dispatch froze with the run loaded: do not free it
    size_t   _fsk_resident_R        = 0;     // header offset of the run's first chunk
    size_t   _fsk_resident_next     = 0;     // structural offset after the last chunk of the run
    fnFile  *_fsk_resident_file     = nullptr;
    size_t   _fsk_resident_filesize = 0;

    FskBoundaryTracker _fsk_bounds{};          // cumulative encoded ticks (monotonic, ISR-only) and the
                                                // ping-pong boundary the next threshold callback will prove;
                                                // see the FskBoundaryTracker rules in fsk_plan.h
    FskRefLog _fsk_ref_log{};                  // hardware-proven positions of this transaction: the seed (start
                                                // position + start timestamp) and the last FSK_REF_RING refill
                                                // references, each stamped with the callback time. A stop at
                                                // time T is positioned from the newest reference not later than T
                                                // (fsk_physical_ticks_at), never from a newer one.
    bool     _fsk_transmission_started    = false; // true only strictly AFTER rmt_transmit() returns ESP_OK; gates
                                                    // every ISR write above so prefill callbacks can never update it

#endif // ESP_PLATFORM
};

#endif
