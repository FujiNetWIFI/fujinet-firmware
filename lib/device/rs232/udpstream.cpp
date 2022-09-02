#ifdef BUILD_RS232

#include "udpstream.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

void rs232UDPStream::rs232_enable_udpstream()
{
    if (udpstream_port == MIDI_PORT)
    {
        // Setup PWM channel for CLOCK IN
        ledc_channel_config_t ledc_channel_rs232_ckin;
        ledc_channel_rs232_ckin.gpio_num = PIN_CKI;
        ledc_channel_rs232_ckin.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_channel_rs232_ckin.channel = LEDC_CHANNEL_1;
        ledc_channel_rs232_ckin.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_rs232_ckin.timer_sel = LEDC_TIMER_1;
        ledc_channel_rs232_ckin.duty = 1;
        ledc_channel_rs232_ckin.hpoint = 0;

        // Setup PWM timer for CLOCK IN
        ledc_timer_config_t ledc_timer;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_timer.duty_resolution = LEDC_TIMER_RESOLUTION;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.freq_hz = MIDI_BAUD;

        // Enable PWM on CLOCK IN
        ledc_channel_config(&ledc_channel_rs232_ckin);
        ledc_timer_config(&ledc_timer);

        // Change baud rate
        fnUartRS232.set_baudrate(MIDI_BAUD);
    }

    // Open the UDP connection
    udpStream.begin(udpstream_port);

    udpstreamActive = true;
#ifdef DEBUG
    Debug_println("UDPSTREAM mode ENABLED");
#endif
}

void rs232UDPStream::rs232_disable_udpstream()
{
    udpStream.stop();
    if (udpstream_port == MIDI_PORT)
    {
        ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);
        fnUartRS232.set_baudrate(RS232_STANDARD_BAUDRATE);
    }
    udpstreamActive = false;
#ifdef DEBUG
    Debug_println("UDPSTREAM mode DISABLED");
#endif
}

void rs232UDPStream::rs232_handle_udpstream()
{
    // if there’s data available, read a packet
    int packetSize = udpStream.parsePacket();
    if (packetSize > 0)
    {
        udpStream.read(buf_net, UDPSTREAM_BUFFER_SIZE);
        // Send to Atari UART
        fnUartRS232.write(buf_net, packetSize);
#ifdef DEBUG_UDPSTREAM
        Debug_print("UDP-IN: ");
        util_dump_bytes(buf_net, packetSize);
#endif
    }

    // Read the data until there's a pause in the incoming stream
    if (fnUartRS232.available() > 0)
    {
        while (true)
        {
            // Break out of UDPStream mode if COMMAND is asserted
            if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
            {
#ifdef DEBUG
                Debug_println("CMD Asserted in LOOP, stopping UDPStream");
#endif
                rs232_disable_udpstream();
                return;
            }
            if (fnUartRS232.available() > 0)
            {
                // Collect bytes read in our buffer
                buf_stream[buf_stream_index] = (char)fnUartRS232.read();
                if (buf_stream_index < UDPSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
            }
            else
            {
                fnSystem.delay_microseconds(UDPSTREAM_PACKET_TIMEOUT);
                if (fnUartRS232.available() <= 0)
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

void rs232UDPStream::rs232_status()
{
    // Nothing to do here
    return;
}

void rs232UDPStream::rs232_process(uint32_t commanddata, uint8_t checksum)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_ATARI */