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
#define NETSTREAM_MIN_GAP_US_MIDI 320                           // ~1 byte at 31.25kbps (fallback)
#define NETSTREAM_MIN_GAP_US_SIO 520                            // ~1 byte at 19.2kbps (fallback)
#define NETSTREAM_PACE_MAX_PER_CALL 64                          // Max bytes drained per pace_to_atari() call.
#define NETSTREAM_RX_DRAIN_MAX_PACKETS 32                       // Max UDP datagrams pulled per service pass.
#define NETSTREAM_RX_MAX_FRAMES 4                               // Default shallow-buffer depth (whole frames). >=2.
#define NETSTREAM_RX_FRAME_SLOTS 64                             // Descriptor ring capacity (>= any rx_depth used).
#define NETSTREAM_RX_BACKPRESSURE_RESERVE 16                    // Stop TCP drains with this much ring headroom left.
#define NETSTREAM_MAX_FRAME 256                                 // Max datagram size buffered on the framed (lossy UDP) path.
#define NETSTREAM_MAX_BATCH_AGE_US 3000                         // Max SIO->NET batch age before forced flush.
#define NETSTREAM_FLUSH_THRESHOLD (NETSTREAM_BUFFER_SIZE - 16)  // Flush when nearly full.
#define NETSTREAM_RX_STATS_INTERVAL_US 1000000                  // Periodic debug stats cadence while active.
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
    uint16_t rx_high_water_mark = 0;
    uint32_t rx_drop_count = 0;

    // Framed (lossy UDP) inbound buffering: a bounded queue of whole datagrams/frames.
    // rx_ring holds concatenated whole *unsent* frames; the frame currently being paced
    // out is copied into inflight[] so it is never in the droppable set (drops stay
    // whole-frame aligned even when pacing breaks mid-frame on the gap).
    bool rx_framed = false;              // true only for UDP non-MIDI
    int rx_cap_frames = NETSTREAM_RX_MAX_FRAMES; // max buffered (waiting) frames
    uint16_t frame_len[NETSTREAM_RX_FRAME_SLOTS]; // per-frame lengths, in order
    uint16_t frame_head = 0;
    uint16_t frame_tail = 0;
    uint16_t frame_count = 0;
    uint8_t inflight[NETSTREAM_MAX_FRAME]; // frame currently being paced out
    int inflight_len = 0;
    int inflight_pos = 0;

    bool cassette_was_active = false;

    uint64_t last_rx_us = 0;
    uint64_t last_tx_us = 0;
    uint64_t last_rx_stats_us = 0;
    bool ensure_netstream_ready();
    void pace_to_atari(uint32_t min_gap_us);
    void update_rx_high_water();
    void log_rx_stats();
    void enqueue_rx(const uint8_t *data, int len);    // byte path enqueue; TCP drains are capped before overflow
    void enqueue_frame(const uint8_t *data, int len); // framed path (lossy UDP): push whole frame, evict oldest whole frames over cap
    void drain_net_to_ring();                         // pull all available net datagrams into the buffer
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
    bool netstream_video_pal = false;
    bool netstream_tx_clock_external = false;
    bool netstream_rx_clock_external = false;
    bool netstream_has_audf3 = false;
    uint8_t netstream_audf3 = 0;
    int netstream_baud = SIO_STANDARD_BAUDRATE;

    void sio_enable_netstream();  // setup netstream
    void sio_disable_netstream(); // stop netstream
    void sio_handle_netstream();  // Handle incoming & outgoing data for netstream
};

#endif
