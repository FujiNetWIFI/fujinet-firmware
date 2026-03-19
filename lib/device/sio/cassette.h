#ifndef CASSETTE_H
#define CASSETTE_H

#include "../../include/pinmap.h"

#ifdef ESP_PLATFORM
#include <driver/rmt_types.h>
#endif

#include "bus.h"
#include "fnSystem.h"
#include "fnio.h"

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
    void sio_status() override{}; // $53, 'S', Status
    void sio_process(uint32_t commanddata, uint8_t checksum) override{};

    void open_cassette_file(FileSystem *filesystem);
    void close_cassette_file();

public:
    void umount_cassette_file();
    void mount_cassette_file(fnFile *f, size_t fz);

    void sio_enable_cassette();  // setup cassette
    void sio_disable_cassette(); // stop cassette
    void sio_handle_cassette();  // Handle incoming & outgoing data for cassette

    void rewind(); // rewind cassette

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
};

#endif
