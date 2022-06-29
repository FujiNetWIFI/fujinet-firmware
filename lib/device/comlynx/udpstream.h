#ifndef UDPSTREAM_H
#define UDPSTREAM_H

#include <driver/ledc.h>

#include "bus.h"

#include "fnUDP.h"

#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_1_BIT

#define UDPSTREAM_BUFFER_SIZE 8192
#define UDPSTREAM_PACKET_TIMEOUT 5000
#define MIDI_PORT 5004
#define MIDI_BAUD 31250

class lynxUDPStream : public virtualDevice
{
private:
    fnUDP udpStream;
    systemBus *_comlynx_bus;

    uint8_t buf_net[UDPSTREAM_BUFFER_SIZE];
    uint8_t buf_stream[UDPSTREAM_BUFFER_SIZE];

    uint8_t buf_stream_index=0;

    void comlynx_process(uint8_t b) override;

public:
    bool udpstreamActive = false; // If we are in udpstream mode or not
    in_addr_t udpstream_host_ip = IPADDR_NONE;
    int udpstream_port;

    void comlynx_enable_udpstream();  // setup udpstream
    void comlynx_disable_udpstream(); // stop udpstream
    void comlynx_handle_udpstream();  // Handle incoming & outgoing data for udpstream
};

#endif