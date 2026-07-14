#ifdef BUILD_ATARI

#include "netstream.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "cassette.h"
#include "fnSystem.h"
#include "fnConfig.h"
#include "utils.h"

#ifdef ESP_PLATFORM
#include <errno.h>
#include <sys/socket.h>
#endif

static uint64_t netstream_time_us()
{
#ifdef ESP_PLATFORM
    return (uint64_t)esp_timer_get_time();
#else
    return (uint64_t)fnSystem.millis() * 1000ULL;
#endif
}

struct NetstreamAudf3Baud
{
    uint8_t audf3;
    int baud;
};

static const NetstreamAudf3Baud kNetstreamAudf3Ntsc[] = {
    {159, 300},   {204, 600},   {162, 750},   {226, 1201},  {109, 2405},
    {179, 4811},  {86, 9622},   {39, 19454},  {21, 31960},  {16, 38908},
    {15, 40677},  {14, 42614},  {13, 44744},  {12, 47099},  {11, 49716},
    {10, 52640},  {9, 55930},   {8, 59659},   {7, 63621},   {6, 68453},
    {5, 74281},   {4, 81444},   {3, 90493},   {2, 102273},  {1, 118250},
    {0, 127841},
};

static const NetstreamAudf3Baud kNetstreamAudf3Pal[] = {
    {132, 300},   {190, 600},   {151, 750},   {219, 1201},  {106, 2403},
    {177, 4819},  {85, 9638},   {39, 19276},  {21, 31668},  {16, 38553},
    {15, 40305},  {14, 42224},  {13, 44336},  {12, 46669},  {11, 49262},
    {10, 52159},  {9, 55420},   {8, 59114},   {7, 63042},   {6, 67829},
    {5, 73604},   {4, 80702},   {3, 89669},   {2, 101341},  {1, 117171},
    {0, 126673},
};

static int netstream_baud_from_audf3(uint8_t audf3, bool is_pal)
{
    const NetstreamAudf3Baud *table = is_pal ? kNetstreamAudf3Pal : kNetstreamAudf3Ntsc;
    size_t count = is_pal ? (sizeof(kNetstreamAudf3Pal) / sizeof(kNetstreamAudf3Pal[0]))
                          : (sizeof(kNetstreamAudf3Ntsc) / sizeof(kNetstreamAudf3Ntsc[0]));
    for (size_t i = 0; i < count; i++)
    {
        if (table[i].audf3 == audf3)
            return table[i].baud;
    }
    return 0;
}

#ifdef ESP_PLATFORM
static void netstream_enable_clock_pwm(int baud)
{
    if (baud <= 0)
        return;

    ledc_timer_config_t ledc_timer = {};
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
    ledc_timer.duty_resolution = LEDC_TIMER_RESOLUTION;
    ledc_timer.timer_num = LEDC_TIMER_1;
    ledc_timer.freq_hz = baud;

    ledc_channel_config_t ledc_channel_sio_ckin = {};
    ledc_channel_sio_ckin.gpio_num = PIN_CKI;
    ledc_channel_sio_ckin.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
    ledc_channel_sio_ckin.channel = LEDC_CHANNEL_1;
    ledc_channel_sio_ckin.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_sio_ckin.timer_sel = LEDC_TIMER_1;
    ledc_channel_sio_ckin.duty = 1;
    ledc_channel_sio_ckin.hpoint = 0;

    ledc_channel_config(&ledc_channel_sio_ckin);
    ledc_timer_config(&ledc_timer);
    ledc_set_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 1);
    ledc_update_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1);
}
#endif

bool sioNetStream::ensure_netstream_ready()
{
    if (netstreamMode == NetStreamMode::UDP)
        return true;
    if (netStreamTcp.connected())
        return true;
    if (netstream_host_ip == IPADDR_NONE || netstream_port <= 0)
        return false;
    if (!netStreamTcp.connect(netstream_host_ip, (uint16_t)netstream_port))
    {
#ifdef DEBUG_NETSTREAM
        Debug_println("NETSTREAM: TCP connect failed");
#endif
        return false;
    }
    netStreamTcp.setNoDelay(true);
    if (netstreamRegisterEnabled)
    {
        const char* str = "REGISTER";
        netStreamTcp.write((const uint8_t *)str, strlen(str));
    }
    buf_stream_index = 0;
    return true;
}

// Inter-byte gap (us) matching the negotiated baud (8N1 => 10 bits/byte).
// The hub's credit flow-control already paces writes to the true line rate, so this
// gap just smooths delivery; fall back to the fixed constants if baud is unknown.
static uint32_t netstream_gap_us_for_baud(int baud, int port)
{
    if (baud > 0)
        return (uint32_t)(10000000u / (uint32_t)baud);
    return (port == MIDI_PORT) ? NETSTREAM_MIN_GAP_US_MIDI : NETSTREAM_MIN_GAP_US_SIO;
}

void sioNetStream::update_rx_high_water()
{
    if (rx_count > rx_high_water_mark)
        rx_high_water_mark = rx_count;
}

void sioNetStream::log_rx_stats()
{
#ifdef DEBUG_NETSTREAM
    uint64_t now_us = netstream_time_us();
    if ((now_us - last_rx_stats_us) < NETSTREAM_RX_STATS_INTERVAL_US)
        return;
    last_rx_stats_us = now_us;
    Debug_printf("NETSTREAM RX stats: drops=%lu high_water=%u/%u count=%u\n",
                 (unsigned long)rx_drop_count,
                 rx_high_water_mark,
                 NETSTREAM_RX_RING_SIZE,
                 rx_count);
#endif
}

void sioNetStream::enqueue_rx(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (rx_count < NETSTREAM_RX_RING_SIZE)
        {
            rx_ring[rx_head] = data[i];
            rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
            rx_count++;
        }
        else
        {
            // Drop oldest byte to keep the most recent stream data.
            rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
            rx_ring[rx_head] = data[i];
            rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
            rx_drop_count++;
        }
        update_rx_high_water();
    }
}

void sioNetStream::enqueue_frame(const uint8_t *data, int len)
{
    // Framed (lossy UDP) path: buffer a whole datagram as one frame, evicting the
    // OLDEST whole frame(s) when over the depth cap or out of physical ring space.
    // Drops are always whole-frame aligned so the Atari deframer never desyncs.
    if (len <= 0)
        return;
    if (len > NETSTREAM_MAX_FRAME)
    {
        // Framed path is for small real-time datagrams; a frame this large can't be
        // held in inflight[]. Drop it whole (keeps the stream frame-aligned).
        rx_drop_count++;
        return;
    }

    while (frame_count > 0 &&
           (frame_count >= rx_cap_frames ||
            frame_count >= NETSTREAM_RX_FRAME_SLOTS ||
            (int)rx_count + len > NETSTREAM_RX_RING_SIZE))
    {
        uint16_t evicted = frame_len[frame_tail];
        frame_tail = (frame_tail + 1) % NETSTREAM_RX_FRAME_SLOTS;
        frame_count--;
        rx_tail = (rx_tail + evicted) % NETSTREAM_RX_RING_SIZE;
        rx_count -= evicted;
        rx_drop_count++;
    }

    for (int i = 0; i < len; i++)
    {
        rx_ring[rx_head] = data[i];
        rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
        rx_count++;
    }
    frame_len[frame_head] = (uint16_t)len;
    frame_head = (frame_head + 1) % NETSTREAM_RX_FRAME_SLOTS;
    frame_count++;
    update_rx_high_water();
}

void sioNetStream::drain_net_to_ring()
{
    if (netstreamMode == NetStreamMode::UDP)
    {
        // Pull every datagram already queued, not just one, so a burst that arrived
        // while we were busy doesn't pile up in the kernel UDP receive buffer.
        int guard = 0;
        int packetSize;
        while ((packetSize = netStreamUdp.parsePacket()) > 0 && guard++ < NETSTREAM_RX_DRAIN_MAX_PACKETS)
        {
            netStreamUdp.read(buf_net, NETSTREAM_BUFFER_SIZE);
            if (rx_framed)
                enqueue_frame(buf_net, packetSize);
            else
                enqueue_rx(buf_net, packetSize);
            last_rx_us = netstream_time_us();
#ifdef DEBUG_NETSTREAM
            Debug_printf("STREAM-IN [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
            util_dump_bytes(buf_net, packetSize);
#endif
        }
    }
    else if (ensure_netstream_ready())
    {
        while (rx_count + NETSTREAM_RX_BACKPRESSURE_RESERVE < NETSTREAM_RX_RING_SIZE)
        {
            size_t free_space = NETSTREAM_RX_RING_SIZE - rx_count;
            if (free_space <= NETSTREAM_RX_BACKPRESSURE_RESERVE)
                break;
            free_space -= NETSTREAM_RX_BACKPRESSURE_RESERVE;
#ifdef ESP_PLATFORM
            size_t to_read = (free_space > NETSTREAM_BUFFER_SIZE) ? NETSTREAM_BUFFER_SIZE : free_space;
            int packetSize = recv(netStreamTcp.fd(), (char *)buf_net, to_read, MSG_DONTWAIT);
            if (packetSize <= 0)
            {
                if (packetSize == 0)
                    netStreamTcp.stop();
                else if (errno != EWOULDBLOCK && errno != EAGAIN)
                    netStreamTcp.stop();
                break;
            }
#else
            size_t available = netStreamTcp.available();
            if (available == 0)
                break;
            size_t to_read = (available > NETSTREAM_BUFFER_SIZE) ? NETSTREAM_BUFFER_SIZE : available;
            if (to_read > free_space)
                to_read = free_space;
            int packetSize = netStreamTcp.read(buf_net, to_read);
            if (packetSize <= 0)
                break;
#endif
            enqueue_rx(buf_net, packetSize);
            last_rx_us = netstream_time_us();
#ifdef DEBUG_NETSTREAM
            Debug_printf("STREAM-IN [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
            util_dump_bytes(buf_net, packetSize);
#endif
        }
    }
    log_rx_stats();
}

void sioNetStream::pace_to_atari(uint32_t min_gap_us)
{
    // Pacing to keep NET->SIO output moving even when other paths wait.
    // Cap bytes per call so a backlog can catch up without holding the SIO service
    // loop long enough to delay CMD-assert detection. The gap (and the hub's credit
    // flow-control) still bound the actual rate, so this can't overrun the Atari.
    uint8_t send_count = 0;

    if (rx_framed)
    {
        // Framed path: send the in-flight frame byte-by-byte; load the next whole frame
        // out of the ring when the current one finishes. The in-flight frame lives in
        // inflight[] (outside the droppable queue), so it is never split by an eviction.
        while (send_count < NETSTREAM_PACE_MAX_PER_CALL)
        {
            if (inflight_pos >= inflight_len)
            {
                if (frame_count == 0)
                    break; // nothing buffered
                int L = frame_len[frame_tail];
                frame_tail = (frame_tail + 1) % NETSTREAM_RX_FRAME_SLOTS;
                frame_count--;
                for (int j = 0; j < L; j++)
                {
                    inflight[j] = rx_ring[rx_tail];
                    rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_count--;
                }
                inflight_len = L;
                inflight_pos = 0;
                if (L == 0)
                    continue;
            }
            uint64_t now_us = netstream_time_us();
            if ((now_us - last_tx_us) < min_gap_us)
                break;
            SYSTEM_BUS.write(&inflight[inflight_pos], 1);
            inflight_pos++;
            last_tx_us += min_gap_us;
            send_count++;
        }
        return;
    }

    // Byte path (lossless MIDI/TCP): drain the ring directly.
    while (rx_count > 0 && send_count < NETSTREAM_PACE_MAX_PER_CALL)
    {
        uint64_t now_us = netstream_time_us();
        if ((now_us - last_tx_us) < min_gap_us)
            break;
        uint8_t out = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
        rx_count--;
        SYSTEM_BUS.write(&out, 1);
        last_tx_us += min_gap_us;
        send_count++;
    }
}

void sioNetStream::sio_enable_netstream()
{
    int baud = 0;

    // Disable cassette so it doesn't interfere with SIO Motor Control toggle
    if (SYSTEM_BUS.getCassette() != nullptr)
    {
        cassette_was_active = SYSTEM_BUS.getCassette()->is_active();
        if (cassette_was_active)
            SYSTEM_BUS.getCassette()->sio_disable_cassette();
    }
    else
    {
        cassette_was_active = false;
    }

    if (netstream_has_audf3)
        baud = netstream_baud_from_audf3(netstream_audf3, netstream_video_pal);
    if (baud <= 0 && netstream_port == MIDI_PORT)
        baud = MIDI_BAUDRATE;
    if (baud <= 0)
        baud = SIO_STANDARD_BAUDRATE;

    netstream_baud = baud;
    // Don't set baud until MOTOR asserted
    //SYSTEM_BUS.set_baudrate(netstream_baud);

#ifdef DEBUG_NETSTREAM
    Debug_printf("NETSTREAM baud: %d (AUDF3=%u %s)\n",
                 netstream_baud,
                 netstream_audf3,
                 netstream_video_pal ? "PAL" : "NTSC");
#endif

#ifdef ESP_PLATFORM
    if (netstream_tx_clock_external || netstream_rx_clock_external)
        netstream_enable_clock_pwm(netstream_baud);
#endif

    if (netstreamMode == NetStreamMode::UDP)
    {
        // Open the UDP connection
        netStreamUdp.begin(netstream_port);
        if (netstreamRegisterEnabled)
        {
            const char* str = "REGISTER";
            netStreamUdp.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
            netStreamUdp.write((const uint8_t *)str, strlen(str));
            netStreamUdp.endPacket();
        }
    }
    else
    {
        // Open the TCP connection
        ensure_netstream_ready();
    }

    netstreamActive = true;
    last_rx_us = netstream_time_us();
    last_tx_us = last_rx_us;
    rx_head = 0;
    rx_tail = 0;
    rx_count = 0;
    rx_high_water_mark = 0;
    rx_drop_count = 0;
    last_rx_stats_us = last_rx_us;

    // Lossy, frame-aligned shallow buffering applies only to UDP non-MIDI streams;
    // MIDI and TCP stay lossless (full byte ring, no proactive drops).
    rx_framed = (netstreamMode == NetStreamMode::UDP) && (netstream_port != MIDI_PORT);
    int cfg_depth = Config.get_network_netstream_rx_depth();
    int depth = (cfg_depth > 0) ? cfg_depth : NETSTREAM_RX_MAX_FRAMES;
    if (depth < 2)
        depth = 2; // need >=2 so an old frame can be shed while one is in flight
    if (depth > NETSTREAM_RX_FRAME_SLOTS - 1)
        depth = NETSTREAM_RX_FRAME_SLOTS - 1;
    rx_cap_frames = depth;
    frame_head = 0;
    frame_tail = 0;
    frame_count = 0;
    inflight_len = 0;
    inflight_pos = 0;

    Debug_println("NETSTREAM mode ENABLED");
}

void sioNetStream::sio_disable_netstream()
{
    netStreamTcp.stop();
    netStreamUdp.stop();
    if (cassette_was_active && SYSTEM_BUS.getCassette() != nullptr && SYSTEM_BUS.getCassette()->is_mounted())
        SYSTEM_BUS.getCassette()->sio_enable_cassette();
    cassette_was_active = false;
#ifdef ESP_PLATFORM
        ledc_stop(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 0);
#endif
        SYSTEM_BUS.setBaudrate(SIO_STANDARD_BAUDRATE);
#ifdef ESP_PLATFORM
    // Reset CKI pin back to output open drain high
    fnSystem.set_pin_mode(PIN_CKI, gpio_mode_t::GPIO_MODE_OUTPUT_OD);
    fnSystem.digital_write(PIN_CKI, DIGI_HIGH);
#endif
    netstreamActive = false;
    Debug_println("NETSTREAM mode DISABLED");
}

void sioNetStream::sio_handle_netstream()
{
    const uint32_t min_gap_us = netstream_gap_us_for_baud(netstream_baud, netstream_port);
    uint64_t batch_start_us = 0;
    bool batch_active = false;

    auto flush_udp_out_batch = [&]()
    {
        if (buf_stream_index == 0)
            return;

        if (netstreamMode == NetStreamMode::UDP)
        {
            if (netstream_host_ip == IPADDR_NONE || netstream_port <= 0)
            {
                buf_stream_index = 0;
                batch_active = false;
                batch_start_us = 0;
                return;
            }
            netStreamUdp.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
            netStreamUdp.write(buf_stream, buf_stream_index);
            netStreamUdp.endPacket();
        }
        else
        {
            if (!ensure_netstream_ready())
            {
                buf_stream_index = 0;
                batch_active = false;
                batch_start_us = 0;
                return;
            }
            netStreamTcp.write(buf_stream, buf_stream_index);
        }

#ifdef DEBUG_NETSTREAM
        Debug_printf("STREAM-OUT [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
        util_dump_bytes(buf_stream, buf_stream_index);
#endif

        buf_stream_index = 0;
        batch_active = false;
        batch_start_us = 0;
    };

    // Pull all queued inbound datagrams into the ring (drop-oldest on overflow).
    drain_net_to_ring();

    pace_to_atari(min_gap_us);

    // Read the data until there's a pause in the incoming stream
    if (SYSTEM_BUS.available() > 0)
    {
        while (true)
        {
            // Break out of NetStream mode if COMMAND is asserted
#ifdef ESP_PLATFORM
            if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
#else
            if (SYSTEM_BUS.commandAsserted())
#endif
            {
                Debug_println("CMD Asserted, stopping NetStream");
                sio_disable_netstream();
                return;
            }
            if (SYSTEM_BUS.available() > 0)
            {
                // Collect bytes read in our buffer
                int in_byte = SYSTEM_BUS.read(); // TODO apc: check for error first
                if (!batch_active)
                {
                    batch_start_us = netstream_time_us();
                    batch_active = true;
                }
                buf_stream[buf_stream_index] = (unsigned char)in_byte;
                if (buf_stream_index < NETSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
                if (buf_stream_index >= NETSTREAM_FLUSH_THRESHOLD)
                {
                    // Flush when nearly full to avoid overwrite/drops.
                    flush_udp_out_batch();
                    continue;
                }
                if (batch_active && (netstream_time_us() - batch_start_us) >= NETSTREAM_MAX_BATCH_AGE_US)
                {
                    // Flush old batches even if the stream never pauses.
                    flush_udp_out_batch();
                    continue;
                }
            }
            else
            {
                // Short, bounded waits prevent starving the pacing loop.
                const uint32_t wait_step_us = 250;
                const uint32_t wait_budget_us = 10000;
                uint32_t waited_us = 0;
                bool flushed = false;
                while (waited_us < wait_budget_us && SYSTEM_BUS.available() <= 0)
                {
                    fnSystem.delay_microseconds(wait_step_us);
                    waited_us += wait_step_us;
                    // Keep refilling from the network so inbound isn't starved while
                    // we wait out an outbound burst (else datagrams pile up upstream).
                    drain_net_to_ring();
                    pace_to_atari(min_gap_us);
                    if (buf_stream_index >= NETSTREAM_FLUSH_THRESHOLD ||
                        (batch_active && (netstream_time_us() - batch_start_us) >= NETSTREAM_MAX_BATCH_AGE_US))
                    {
                        flush_udp_out_batch();
                        flushed = true;
                        break;
                    }
                }
                if (flushed && SYSTEM_BUS.available() > 0)
                    continue;
                if (SYSTEM_BUS.available() <= 0)
                    break;
            }
        }

        // Send what we've collected after a pause.
        flush_udp_out_batch();
    }

    // Pace again after serial->UDP handling to avoid starving during outbound bursts.
    pace_to_atari(min_gap_us);
}

void sioNetStream::sio_status()
{
    // Nothing to do here
    return;
}

void sioNetStream::sio_process(uint32_t commanddata, uint8_t checksum)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_ATARI */
