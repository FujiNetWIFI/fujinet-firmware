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

    void sio_status() override; // $53, 'S', Status
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool cassetteActive = false; // If we are in cassette mode or not

    void open_cassette_file(FileSystem *filesystem); // open a file
    void sio_enable_cassette();                      // setup cassette
    void sio_disable_cassette();                     // stop cassette
    void sio_handle_cassette();                      // Handle incoming & outgoing data for cassette

private:
    struct tape_FUJI_hdr
    {
        char chunk_type[4];
        unsigned short chunk_length;
        unsigned short irg_length;
        char data[];
    };

    struct t_flags
    {
        unsigned char run : 1;
        unsigned char FUJI : 1;
        unsigned char turbo : 1;
    } tape_flags;

    unsigned int send_tape_block(unsigned int offset);
    void check_for_FUJI_file();
    unsigned int send_FUJI_tape_block(unsigned int offset);
};

#endif