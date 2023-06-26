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

class sioUDPStream : public virtualDevice
{
private:
    fnUDP udpStream;

    uint8_t buf_net[UDPSTREAM_BUFFER_SIZE];
    uint8_t buf_stream[UDPSTREAM_BUFFER_SIZE];

    uint8_t buf_stream_index=0;

    void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool udpstreamActive = false; // If we are in udpstream mode or not
    in_addr_t udpstream_host_ip = IPADDR_NONE;
    int udpstream_port;

    void sio_enable_udpstream();  // setup udpstream
    void sio_disable_udpstream(); // stop udpstream
    void sio_handle_udpstream();  // Handle incoming & outgoing data for udpstream
};

#endif