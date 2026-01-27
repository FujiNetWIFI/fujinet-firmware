#ifndef NETSTREAM_H
#define NETSTREAM_H

#include <driver/ledc.h>

#include "bus.h"

#include "fnUDP.h"
#include "fnTcpClient.h"

#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_1_BIT

#define NETSTREAM_BUFFER_SIZE 8192
#define NETSTREAM_PACKET_TIMEOUT 5000
#define MIDI_PORT 5004
#define MIDI_BAUDRATE 31250

class lynxNetStream : public virtualDevice
{
private:
    fnUDP netStreamUdp;
    fnTcpClient netStreamTcp;
    uint8_t buf_net[NETSTREAM_BUFFER_SIZE];
    uint8_t buf_stream[NETSTREAM_BUFFER_SIZE];

    uint8_t buf_stream_index=0;

    bool ensure_tcp_connected();
    void comlynx_process(uint8_t b) override;

public:
    enum class NetStreamMode : uint8_t
    {
        UDP = 0,
        TCP = 1
    };

    NetStreamMode netstreamMode = NetStreamMode::UDP;
    bool netstreamActive = false; // If we are in netstream mode or not
    in_addr_t netstream_host_ip = IPADDR_NONE;
    int netstream_port;

    void comlynx_enable_netstream();  // setup netstream
    void comlynx_disable_netstream(); // stop netstream
    void comlynx_handle_netstream();  // Handle incoming & outgoing data for netstream
};

#endif
