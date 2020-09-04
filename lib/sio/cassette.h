#ifndef CASSETTE_H
#define CASSETTE_H

//#include <driver/ledc.h>
#include "sio.h"
//#include "../tcpip/fnUDP.h"

#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_1_BIT

#define CASSETTE_BAUD 600

class sioCassette : public sioDevice
{
private:

    void sio_status() override;                  // $53, 'S', Status
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool cassetteActive = false; // If we are in cassette mode or not

    void sio_enable_cassette();  // setup cassette
    void sio_disable_cassette(); // stop cassette
    void sio_handle_cassette();  // Handle incoming & outgoing data for cassette
};

#endif