#ifndef CASSETTE_H
#define CASSETTE_H

#include "../../include/pinmap.h"

#ifdef ESP_PLATFORM
#include <driver/rmt_types.h>
#include <esp_heap_caps.h> // ESP-only: FSK pointer table in internal 8-bit DRAM; payload blocks in 8-bit PSRAM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h> // SemaphoreHandle_t for _cassette_lock
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
    bool rewind_seconds(uint32_t seconds); // rewind by N seconds using the real per-chunk duration model

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
    // Created once in the constructor, for the lifetime of this (singleton)
    // object; never recreated. Guards tape_offset, _file, and every format
    // state field (baud/t2k_*/qros_turbo_baud) against the cross-task race
    // between the HTTP server task (rewind()/rewind_seconds()) and the
    // fnService loop task (sio_handle_cassette()). sio_handle_cassette()
    // holds this for its whole dispatch, including any in-flight
    // rmt_tx_wait_all_done() — a Custom Rewind requested mid-waveform waits
    // for that transaction to finish before it can proceed. No cancellation.
    SemaphoreHandle_t _cassette_lock = nullptr;
#endif

    // Walks A8CAS/FUJI chunk headers (or, for a non-FUJI file, fixed 128-byte
    // legacy blocks) from file offset 0, accumulating the exact real playback
    // duration using only on-disk fields, and reports the format state that
    // would be active at the resulting offset. Used both to resolve the
    // CURRENT position's elapsed time (stop_at_offset = tape_offset,
    // stop_at_time_us = UINT64_MAX) and to resolve a TARGET time back to a
    // chunk-boundary offset (stop_at_offset = SIZE_MAX). Caller must hold
    // _cassette_lock — this function seeks/reads the shared _file handle and
    // restores its original position before returning. Returns false only on
    // a structural walk failure (not a valid FUJI/legacy file at offset 0).
    bool walk_tape_time(size_t stop_at_offset, uint64_t stop_at_time_us,
                        CassetteWalkState &out) const;

    // Single choke point called before ANY tape_offset write from rewind()/
    // rewind_seconds(): stops any in-flight FSK/Turbo2000/QROS waveform,
    // frees the FSK preload block table, and frees the Turbo 2000 pending
    // buffer. Caller must hold _cassette_lock. Idempotent — safe to call when
    // nothing is active.
    void stop_and_reset_for_reposition();

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

    // FSK chunk playback (A8CAS "fsk " chunks) — cross-platform entry point.
    // PRELOADS the whole clamped payload into a small block table via a bounded
    // read-loop (before any emission), honors the IRG, reproduces the raw FSK
    // signal via the RMT stateful simple encoder fed from that IMMUTABLE resident
    // block table (ESP), or safely skips (PC), and returns the next read offset.
    // Never changes the active baud. Holds NO full-waveform buffer; the payload IS
    // resident (as a block table) but there is no in-flight mutation and no file
    // I/O during emission.
    size_t play_fsk_chunk(size_t offset, uint16_t chunk_length, uint16_t irg_ms);

#ifdef ESP_PLATFORM
    // Segmented preload block size. Small (kind to a fragmented no-PSRAM heap)
    // and, being <= the TNFS per-read limit, fillable by a single fnio::fread.
    // A8CAS caps the payload at 65535 bytes, so at 512 bytes/block the pointer
    // table is <= 128 entries. A payload that fits one block uses the contiguous
    // fast path.
    static constexpr size_t FSK_PRELOAD_BLOCK_BYTES = 512;

    // Conservative maximum requested in one preload read. Current FujiNet TNFS
    // allows at most 525 bytes; 512 stays below that limit and matches the block
    // size. Positive short reads are accumulated until the clamped payload is
    // complete.
    static constexpr size_t FSK_PRELOAD_READ_MAX = 512;

    // Raw FSK signal helpers built on the ESP RMT peripheral (same PIN_UART2_TX
    // and detach/reattach approach as Turbo 2000, and the same stateful simple
    // encoder pattern as t2k_encode_cb / rmt_new_simple_encoder).
    bool fsk_signal_begin();   // alloc RMT channel + simple encoder (callback=fsk_encode_cb, arg=this), detach UART TX; false on failure
    void fsk_signal_emit();    // ONE rmt_transmit using the immutable pointer table as transaction payload; then wait-done
    void fsk_signal_end();     // idempotent: teardown RMT + encoder, reattach UART TX

    void fsk_free_blocks();    // free every preloaded block + the pointer table; idempotent, safe after partial preload

    // Preload a contiguous zero-IRG FSK run into ONE PSRAM block table with each
    // chunk aligned to its own block boundary, filling the run descriptors
    // (_fsk_run_*). run_offsets/run_data_avail hold each chunk's payload start
    // offset and clamped byte count; count is the number of chunks (1..MAX).
    // Returns true with the whole run resident + immutable; on ANY failure frees
    // everything and returns false (no partial waveform). Reads stay <=512 bytes.
    bool fsk_preload_run(const size_t *run_offsets, const size_t *run_data_avail,
                         size_t count);

    // The stateful RMT simple-encoder callback (same 7-arg signature as
    // t2k_encode_cb). Generates rmt_symbol_word_t on demand from the IMMUTABLE
    // resident block table plus the O(1) encoder cursor below. NO file I/O, NO
    // heap allocation. The simple encoder is configured with min_chunk_size = 1;
    // when work remains the callback produces at least one symbol, otherwise it
    // sets *done. It never returns 0 to wait for source data.
    static size_t IRAM_ATTR fsk_encode_cb(const void *data, size_t data_size,
                                          size_t symbols_written, size_t symbols_free,
                                          rmt_symbol_word_t *symbols, bool *done, void *arg);

    void       *_fsk_rmt_channel = nullptr;
    void       *_fsk_rmt_encoder = nullptr;
    bool        _fsk_signal_active = false;

    // --- Preloaded payload as a block table (fully resident BEFORE rmt_transmit,
    //     IMMUTABLE during the transaction). A payload that fits one block is a
    //     single-element table (the contiguous fast path). Freed only after
    //     rmt_tx_wait_all_done, in the single cleanup path. ---
    uint8_t  **_fsk_blocks          = nullptr; // pointer table: _fsk_block_count entries
    size_t     _fsk_block_size      = 0;       // bytes per block (final block may be partly used)
    size_t     _fsk_block_count     = 0;       // number of blocks allocated
    size_t     _fsk_payload_len     = 0;       // total clamped logical payload bytes (0..65535)

    // --- O(1) ISR-only encoder cursor over the immutable block table
    //     (set before rmt_transmit; advanced ONLY by fsk_encode_cb in the ISR;
    //     no task mutates it during the transaction) ---
    size_t   _fsk_value_count       = 0;       // floor(_fsk_payload_len / 2); done when index reaches this
    size_t   _fsk_value_index       = 0;       // current original FSK value index (for parity)
    size_t   _fsk_payload_pos       = 0;       // logical payload byte position consumed by the encoder (== value_index*2)
    uint32_t _fsk_remaining_ticks   = 0;       // ticks left for the value being split (15-bit carry)
    bool     _fsk_level_high        = false;   // logical level of the value being split (index parity)

    // --- Contiguous zero-IRG FSK RUN descriptors ---
    // A run is a maximal sequence of consecutive `fsk ` chunks where only the
    // first carries a non-zero IRG and every following chunk carries IRG == 0.
    // The whole run is preloaded into ONE PSRAM block table and reproduced with
    // ONE RMT begin/emit/end lifecycle so no gap is inserted at A8CAS container
    // boundaries. Each chunk keeps its OWN value_count (floor(len/2), odd tail
    // ignored per chunk) and starts on its OWN block boundary so per-chunk index
    // parity resets cleanly at each boundary. These small descriptors are
    // INTERNAL; only the payload blocks live in PSRAM.
    size_t   _fsk_run_chunk_count   = 0;       // number of chunks in the current run (>=1)
    size_t   _fsk_run_value_counts[FSK_RUN_MAX_CHUNKS] = {}; // per-chunk value_count
    size_t   _fsk_run_block_base[FSK_RUN_MAX_CHUNKS]   = {}; // per-chunk first block index
    // Encoder-cursor position within the run (ISR-only, set before rmt_transmit):
    size_t   _fsk_run_chunk_index   = 0;       // which run chunk the cursor is in

#endif // ESP_PLATFORM
};

#endif
