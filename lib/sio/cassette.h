#ifndef CASSETTE_H
#define CASSETTE_H

//#include <driver/ledc.h>
#include "sio.h"
//#include "../tcpip/fnUDP.h"

#define CASSETTE_BAUD 600
#define BLOCK_LEN 128

class sioCassette : public sioDevice
{
protected:
    FileSystem *_FS = nullptr;
    FILE *_file = nullptr;
    size_t filesize = 0;

    bool _mounted = false;

    void sio_status() override {}; // $53, 'S', Status
    void sio_process(uint32_t commanddata, uint8_t checksum) override {};

public:
    bool cassetteActive = false; // If we are in cassette mode or not

    void open_cassette_file(FileSystem *filesystem); // open a file
    void close_cassette_file();

    void sio_enable_cassette();                      // setup cassette
    void sio_disable_cassette();                     // stop cassette
    void sio_handle_cassette();                      // Handle incoming & outgoing data for cassette

    bool is_mounted() { return _mounted; };

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
        unsigned char run : 1;
        unsigned char FUJI : 1;
        unsigned char turbo : 1;
    } tape_flags;

    uint8_t atari_sector_buffer[256];
    //struct FileInfoStruct FileInfo;
    void Clear_atari_sector_buffer(uint16_t len);

    unsigned short block;
    unsigned short baud;

    unsigned int send_tape_block(unsigned int offset);
    void check_for_FUJI_file();
    unsigned int send_FUJI_tape_block(unsigned int offset);
};

#endif