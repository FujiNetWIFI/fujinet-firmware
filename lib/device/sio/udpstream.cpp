#ifdef BUILD_ATARI

#include "udpstream.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

void sioUDPStream::sio_enable_udpstream()
{
    if (udpstream_port == MIDI_PORT)
    {
#ifdef ESP_PLATFORM
        // Setup PWM channel for CLOCK IN
        ledc_channel_config_t ledc_channel_sio_ckin;
        ledc_channel_sio_ckin.gpio_num = PIN_CKI;
        ledc_channel_sio_ckin.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
        ledc_channel_sio_ckin.channel = LEDC_CHANNEL_1;
        ledc_channel_sio_ckin.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_sio_ckin.timer_sel = LEDC_TIMER_1;
        ledc_channel_sio_ckin.duty = 1;
        ledc_channel_sio_ckin.hpoint = 0;

        // Setup PWM timer for CLOCK IN
        ledc_timer_config_t ledc_timer;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
        ledc_timer.duty_resolution = LEDC_TIMER_RESOLUTION;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.freq_hz = MIDI_BAUDRATE;

        // Enable PWM on CLOCK IN
        ledc_channel_config(&ledc_channel_sio_ckin);
        ledc_timer_config(&ledc_timer);
        ledc_set_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 1);
        ledc_update_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1);
#endif

        // Change baud rate
        SYSTEM_BUS.setBaudrate(MIDI_BAUDRATE);
    }

    // Open the UDP connection
    udpStream.begin(udpstream_port);

    udpstreamActive = true;
    Debug_println("UDPSTREAM mode ENABLED");
    if (udpstreamIsServer)
    {
        // Register with the server
        Debug_println("UDPSTREAM registering with server");
        const char* str = "REGISTER";
        memcpy(buf_stream, str, strlen(str));
        udpStream.beginPacket(udpstream_host_ip, udpstream_port); // remote IP and port
        udpStream.write(buf_stream, strlen(str));
        udpStream.endPacket();
        // number the outgoing packet for the server to handle sequencing
        packet_seq = 0;
        buf_stream_index = 0;
        packet_seq += 1;
        *(uint16_t *)buf_stream = packet_seq;
        buf_stream_index += 2;
    }
}

void sioUDPStream::sio_disable_udpstream()
{
    udpStream.stop();
    if (udpstream_port == MIDI_PORT)
    {
#ifdef ESP_PLATFORM
        ledc_stop(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 0);
#endif
        SYSTEM_BUS.setBaudrate(SIO_STANDARD_BAUDRATE);
    }
#ifdef ESP_PLATFORM
    // Reset CKI pin back to output open drain high
    fnSystem.set_pin_mode(PIN_CKI, gpio_mode_t::GPIO_MODE_OUTPUT_OD);
    fnSystem.digital_write(PIN_CKI, DIGI_HIGH);
#endif
    udpstreamActive = false;
    udpstreamIsServer = false;
    Debug_println("UDPSTREAM mode DISABLED");
}

void sioUDPStream::sio_handle_udpstream()
{
    // if there’s data available, read a packet
    int packetSize = udpStream.parsePacket();
    if (packetSize > 0)
    {
        udpStream.read(buf_net, UDPSTREAM_BUFFER_SIZE);
        // Send to Atari UART
        SYSTEM_BUS.write(buf_net, packetSize);
#ifdef ESP_PLATFORM
        if (udpstreamIsServer)
        {
            // Reset the timer
            start = (uint32_t)esp_timer_get_time();
        }
#endif
#ifdef DEBUG_UDPSTREAM
        Debug_print("UDP-IN: ");
        util_dump_bytes(buf_net, packetSize);
#endif
    }

#ifdef ESP_PLATFORM
/*   // Send a fake keep alive packet if it's taking too long. This isn't working
    if ((uint32_t)esp_timer_get_time() - start >= UDPSTREAM_KEEPALIVE_TIMEOUT
        && fnSystem.digital_read(PIN_MTR) == DIGI_HIGH) // only if MTR line is on
    {
        fnUartSIO.write(0x00);
        Debug_println("UDPSTREAM: Fake SIO keep alive packet");
        start = (uint32_t)esp_timer_get_time();
    }
*/
#endif

    // Read the data until there's a pause in the incoming stream
    if (SYSTEM_BUS.available() > 0)
    {
        while (true)
        {
            // Break out of UDPStream mode if COMMAND is asserted
#ifdef ESP_PLATFORM
            if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
#else
            if (SYSTEM_BUS.command_asserted())
#endif
            {
                Debug_println("CMD Asserted in LOOP, stopping UDPStream");
                sio_disable_udpstream();
                return;
            }
            if (SYSTEM_BUS.available() > 0)
            {
                // Collect bytes read in our buffer
                buf_stream[buf_stream_index] = (unsigned char)SYSTEM_BUS.read(); // TODO apc: check for error first
                if (buf_stream_index < UDPSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
            }
            else
            {
                fnSystem.delay_microseconds(UDPSTREAM_PACKET_TIMEOUT);
                if (SYSTEM_BUS.available() <= 0)
                    break;
            }
        }

        // Send what we've collected
        udpStream.beginPacket(udpstream_host_ip, udpstream_port); // remote IP and port
        udpStream.write(buf_stream, buf_stream_index);
        udpStream.endPacket();

#ifdef DEBUG_UDPSTREAM
        Debug_print("UDP-OUT: ");
        util_dump_bytes(buf_stream, buf_stream_index);
#endif
        buf_stream_index = 0;
        if (udpstreamIsServer)
        {
            // number the outgoing packet for the server to handle sequencing
            packet_seq += 1;
            *(uint16_t *)buf_stream = packet_seq;
            buf_stream_index += 2;
        }
    }
}

void sioUDPStream::sio_status()
{
    // Nothing to do here
    return;
}

void sioUDPStream::sio_process(uint32_t commanddata, uint8_t checksum)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_ATARI */
