#ifdef BUILD_ATARI

#include "netstream.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "cassette.h"
#include "fnSystem.h"
#include "utils.h"

// TODO: merge/fix this at global level
#ifdef ESP_PLATFORM
#include "fnUART.h"
#define FN_BUS_LINK fnUartBUS
#else
#define FN_BUS_LINK fnSioCom
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

void sioNetStream::pace_to_atari(uint32_t min_gap_us)
{
    // Pacing to keep NET->SIO output moving even when other paths wait.
    uint8_t send_count = 0;
    while (rx_count > 0 && send_count < 16)
    {
        uint64_t now_us = netstream_time_us();
        if ((now_us - last_tx_us) < min_gap_us)
            break;
        uint8_t out = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
        rx_count--;
        FN_BUS_LINK.write(&out, 1);
        last_tx_us += min_gap_us;
        send_count++;
    }
}

void sioNetStream::sio_enable_netstream()
{
    int baud = 0;

    // Disable cassette so it doesn't interfere with SIO Motor Control toggle
    if (SIO.getCassette() != nullptr)
    {
        cassette_was_active = SIO.getCassette()->is_active();
        if (cassette_was_active)
            SIO.getCassette()->sio_disable_cassette();
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
    //FN_BUS_LINK.set_baudrate(netstream_baud);

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
    rx_drop_count = 0;
    Debug_println("NETSTREAM mode ENABLED");
}

void sioNetStream::sio_disable_netstream()
{
    netStreamTcp.stop();
    netStreamUdp.stop();
    if (cassette_was_active && SIO.getCassette() != nullptr && SIO.getCassette()->is_mounted())
        SIO.getCassette()->sio_enable_cassette();
    cassette_was_active = false;
#ifdef ESP_PLATFORM
        ledc_stop(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 0);
#endif
        FN_BUS_LINK.set_baudrate(SIO_STANDARD_BAUDRATE);
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
    const uint32_t min_gap_us = (netstream_port == MIDI_PORT) ? NETSTREAM_MIN_GAP_US_MIDI : NETSTREAM_MIN_GAP_US_SIO;
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

    if (netstreamMode == NetStreamMode::UDP)
    {
        // if there’s data available, read a packet
        int packetSize = netStreamUdp.parsePacket();
        if (packetSize > 0)
        {
            netStreamUdp.read(buf_net, NETSTREAM_BUFFER_SIZE);

            // Buffer incoming UDP bytes for paced UART output.
            for (int i = 0; i < packetSize; i++)
            {
                if (rx_count < NETSTREAM_RX_RING_SIZE)
                {
                    rx_ring[rx_head] = buf_net[i];
                    rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_count++;
                }
                else
                {
                    // Drop oldest byte to keep the most recent stream data.
                    rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_ring[rx_head] = buf_net[i];
                    rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_drop_count++;
                }
            }
            last_rx_us = netstream_time_us();
#ifdef DEBUG_NETSTREAM
            Debug_printf("STREAM-IN [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
            util_dump_bytes(buf_net, packetSize);
#endif
        }
    }
    else if (ensure_netstream_ready())
    {
        // if there’s data available, read from the TCP stream
        size_t available = netStreamTcp.available();
        while (available > 0)
        {
            size_t to_read = (available > NETSTREAM_BUFFER_SIZE) ? NETSTREAM_BUFFER_SIZE : available;
            int packetSize = netStreamTcp.read(buf_net, to_read);
            if (packetSize <= 0)
                break;

            // Buffer incoming TCP bytes for paced UART output.
            for (int i = 0; i < packetSize; i++)
            {
                if (rx_count < NETSTREAM_RX_RING_SIZE)
                {
                    rx_ring[rx_head] = buf_net[i];
                    rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_count++;
                }
                else
                {
                    // Drop oldest byte to keep the most recent stream data.
                    rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_ring[rx_head] = buf_net[i];
                    rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_drop_count++;
                }
            }
            last_rx_us = netstream_time_us();
#ifdef DEBUG_NETSTREAM
            Debug_printf("STREAM-IN [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
            util_dump_bytes(buf_net, packetSize);
#endif
            available = netStreamTcp.available();
        }
    }

    pace_to_atari(min_gap_us);

    // Read the data until there's a pause in the incoming stream
    if (FN_BUS_LINK.available() > 0)
    {
        while (true)
        {
            // Break out of NetStream mode if COMMAND is asserted
#ifdef ESP_PLATFORM
            if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
#else
            if (FN_BUS_LINK.command_asserted())
#endif
            {
                Debug_println("CMD Asserted, stopping NetStream");
                sio_disable_netstream();
                return;
            }
            if (FN_BUS_LINK.available() > 0)
            {
                // Collect bytes read in our buffer
                int in_byte = FN_BUS_LINK.read(); // TODO apc: check for error first
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
                while (waited_us < wait_budget_us && FN_BUS_LINK.available() <= 0)
                {
                    fnSystem.delay_microseconds(wait_step_us);
                    waited_us += wait_step_us;
                    pace_to_atari(min_gap_us);
                    if (buf_stream_index >= NETSTREAM_FLUSH_THRESHOLD ||
                        (batch_active && (netstream_time_us() - batch_start_us) >= NETSTREAM_MAX_BATCH_AGE_US))
                    {
                        flush_udp_out_batch();
                        flushed = true;
                        break;
                    }
                }
                if (flushed && FN_BUS_LINK.available() > 0)
                    continue;
                if (FN_BUS_LINK.available() <= 0)
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
