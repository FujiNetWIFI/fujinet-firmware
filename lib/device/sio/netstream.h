#ifndef NETSTREAM_H
#define NETSTREAM_H

#ifdef ESP_PLATFORM
#include <driver/ledc.h>
#include "sdkconfig.h"
#endif

#include "bus.h"

#include "fnTcpClient.h"
#include "fnUDP.h"

#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_1_BIT

#ifdef ESP_PLATFORM
#ifdef CONFIG_IDF_TARGET_ESP32
   #define LEDC_ESP32XX_HIGH_SPEED LEDC_HIGH_SPEED_MODE
#else
   #define LEDC_ESP32XX_HIGH_SPEED LEDC_LOW_SPEED_MODE
#endif
#endif

#define NETSTREAM_BUFFER_SIZE 2048
#define NETSTREAM_RX_RING_SIZE 2048
#define NETSTREAM_PACKET_TIMEOUT 5000
#define NETSTREAM_MIN_GAP_US_MIDI 320                           // ~1 byte at 31.25kbps
#define NETSTREAM_MIN_GAP_US_SIO 520                            // ~1 byte at 19.2kbps
#define NETSTREAM_MAX_BATCH_AGE_US 3000                         // Max SIO->NET batch age before forced flush.
#define NETSTREAM_FLUSH_THRESHOLD (NETSTREAM_BUFFER_SIZE - 16)  // Flush when nearly full.
#define MIDI_PORT 5004
#define MIDI_BAUDRATE 31250

class sioNetStream : public virtualDevice
{
private:
    fnTcpClient netStreamTcp;
    fnUDP netStreamUdp;

    uint8_t buf_net[NETSTREAM_BUFFER_SIZE];
    uint8_t buf_stream[NETSTREAM_BUFFER_SIZE];

    unsigned int buf_stream_index=0;

    uint8_t rx_ring[NETSTREAM_RX_RING_SIZE];
    uint16_t rx_head = 0;
    uint16_t rx_tail = 0;
    uint16_t rx_count = 0;
    uint32_t rx_drop_count = 0;
    bool cassette_was_active = false;

    uint64_t last_rx_us = 0;
    uint64_t last_tx_us = 0;
    bool ensure_netstream_ready();
    void pace_to_atari(uint32_t min_gap_us);
    void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    enum class NetStreamMode : uint8_t
    {
        UDP = 0,
        TCP = 1
    };

    bool netstreamActive = false; // If we are in netstream mode or not
    bool netstreamRegisterEnabled = true; // Send REGISTER on connect
    NetStreamMode netstreamMode = NetStreamMode::TCP;
    in_addr_t netstream_host_ip = IPADDR_NONE;
    int netstream_port;

    void sio_enable_netstream();  // setup netstream
    void sio_disable_netstream(); // stop netstream
    void sio_handle_netstream();  // Handle incoming & outgoing data for netstream
};

#endif
