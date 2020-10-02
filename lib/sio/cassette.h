#ifndef CASSETTE_H
#define CASSETTE_H

//#include <driver/ledc.h>
#include "sio.h"
//#include "../tcpip/fnUDP.h"

#define CASSETTE_BAUD 600
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
    uint32_t baud = CASSETTE_BAUD;             // bps
    uint32_t period = 1000000 / CASSETTE_BAUD; // microseconds

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
    uint8_t read();
    int8_t service(uint8_t b);
};

class sioCassette : public sioDevice
{
protected:
    FileSystem *_FS = nullptr;
    FILE *_file = nullptr;
    size_t filesize = 0;

    bool _mounted = false;

    void sio_status() override{}; // $53, 'S', Status
    void sio_process(uint32_t commanddata, uint8_t checksum) override{};

    cassette_mode_t cassetteMode = cassette_mode_t::record; // If we are in cassette mode or not
    bool cassetteActive = false;

    // FSK demod (from Atari and maybe from WAV)
    uint64_t fsk_clock; // can count period width from atari because
    uint8_t last_value;
    const uint16_t period_space = 1000000 / 3995;
    const uint16_t period_mark = 1000000 / 5327;
    void detect_falling_edge();
    uint8_t decode_fsk();

public:
    void open_cassette_file(FileSystem *filesystem); // open a file
    void close_cassette_file();

    void sio_enable_cassette();  // setup cassette
    void sio_disable_cassette(); // stop cassette
    void sio_handle_cassette();  // Handle incoming & outgoing data for cassette

    bool is_mounted() { return _mounted; };
    bool is_active() { return cassetteActive; };

private:
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
    } tape_flags;

    uint8_t atari_sector_buffer[256];

    void Clear_atari_sector_buffer(uint16_t len);

    unsigned short block;
    unsigned short baud;

    unsigned int send_tape_block(unsigned int offset);
    void check_for_FUJI_file();
    unsigned int send_FUJI_tape_block(unsigned int offset);
    unsigned int receive_FUJI_tape_block(unsigned int offset);
};

#endif