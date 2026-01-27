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

    if (netstream_port == MIDI_PORT)
    {
#ifdef ESP_PLATFORM
        // Setup PWM timer for CLOCK IN
        ledc_timer_config_t ledc_timer = {};
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
        ledc_timer.duty_resolution = LEDC_TIMER_RESOLUTION;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.freq_hz = MIDI_BAUDRATE;

        // Setup PWM channel for CLOCK IN
        ledc_channel_config_t ledc_channel_sio_ckin = {};
        ledc_channel_sio_ckin.gpio_num = PIN_CKI;
        ledc_channel_sio_ckin.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
        ledc_channel_sio_ckin.channel = LEDC_CHANNEL_1;
        ledc_channel_sio_ckin.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_sio_ckin.timer_sel = LEDC_TIMER_1;
        ledc_channel_sio_ckin.duty = 1;
        ledc_channel_sio_ckin.hpoint = 0;

        // Enable PWM on CLOCK IN
        ledc_channel_config(&ledc_channel_sio_ckin);
        ledc_timer_config(&ledc_timer);
        ledc_set_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 1);
        ledc_update_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1);
#endif

        // Change baud rate
        FN_BUS_LINK.set_baudrate(MIDI_BAUDRATE);
    }

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
