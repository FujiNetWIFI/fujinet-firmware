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
        // Setup PWM channel for CLOCK IN
        ledc_channel_config_t ledc_channel_sio_ckin;
        ledc_channel_sio_ckin.gpio_num = PIN_CKI;
        ledc_channel_sio_ckin.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_channel_sio_ckin.channel = LEDC_CHANNEL_1;
        ledc_channel_sio_ckin.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_sio_ckin.timer_sel = LEDC_TIMER_1;
        ledc_channel_sio_ckin.duty = 1;
        ledc_channel_sio_ckin.hpoint = 0;

        // Setup PWM timer for CLOCK IN
        ledc_timer_config_t ledc_timer;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_timer.duty_resolution = LEDC_TIMER_RESOLUTION;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.freq_hz = MIDI_BAUD;

        // Enable PWM on CLOCK IN
        ledc_channel_config(&ledc_channel_sio_ckin);
        ledc_timer_config(&ledc_timer);

        // Change baud rate
        fnSioLink.set_baudrate(MIDI_BAUD);
    }

    // Open the UDP connection
    udpStream.begin(udpstream_port);

    udpstreamActive = true;
#ifdef DEBUG
    Debug_println("UDPSTREAM mode ENABLED");
#endif
}

void sioUDPStream::sio_disable_udpstream()
{
    udpStream.stop();
    if (udpstream_port == MIDI_PORT)
    {
        ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);
        fnSioLink.set_baudrate(SIO_STANDARD_BAUDRATE);
    }
    udpstreamActive = false;
#ifdef DEBUG
    Debug_println("UDPSTREAM mode DISABLED");
#endif
}

void sioUDPStream::sio_handle_udpstream()
{
    // if there’s data available, read a packet
    int packetSize = udpStream.parsePacket();
    if (packetSize > 0)
    {
        udpStream.read(buf_net, UDPSTREAM_BUFFER_SIZE);
        // Send to Atari UART
        fnSioLink.write(buf_net, packetSize);
#ifdef DEBUG_UDPSTREAM
        Debug_print("UDP-IN: ");
        util_dump_bytes(buf_net, packetSize);
#endif
    }

    // Read the data until there's a pause in the incoming stream
    if (fnSioLink.available() > 0)
    {
        while (true)
        {
            // Break out of UDPStream mode if COMMAND is asserted
            if (fnSioLink.command_asserted())
            {
#ifdef DEBUG
                Debug_println("CMD Asserted in LOOP, stopping UDPStream");
#endif
                sio_disable_udpstream();
                return;
            }
            if (fnSioLink.available() > 0)
            {
                // Collect bytes read in our buffer
                buf_stream[buf_stream_index] = (char)fnSioLink.read();
                if (buf_stream_index < UDPSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
            }
            else
            {
                fnSystem.delay_microseconds(UDPSTREAM_PACKET_TIMEOUT);
                if (fnSioLink.available() <= 0)
                    break;
            }
        }

        // Send what we've collected over WiFi
        udpStream.beginPacket(udpstream_host_ip, udpstream_port); // remote IP and port
        udpStream.write(buf_stream, buf_stream_index);
        udpStream.endPacket();

#ifdef DEBUG_UDPSTREAM
        Debug_print("UDP-OUT: ");
        util_dump_bytes(buf_stream, buf_stream_index);
#endif
        buf_stream_index = 0;
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