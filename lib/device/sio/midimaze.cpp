#include "../../include/debug.h"
#include "fnSystem.h"
#include "utils.h"
#include "midimaze.h"

void sioMIDIMaze::sio_enable_midimaze()
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

    // Open the UDP connection
    udpMIDI.begin(MIDIMAZE_PORT);

    // Change baud rate
    fnUartSIO.set_baudrate(MIDI_BAUD);
    midimazeActive = true;
#ifdef DEBUG
    Debug_println("MIDIMAZE mode enabled");
#endif
}

void sioMIDIMaze::sio_disable_midimaze()
{
    ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);
    udpMIDI.stop();
    fnUartSIO.set_baudrate(SIO_STANDARD_BAUDRATE);
    midimazeActive = false;
}

void sioMIDIMaze::sio_handle_midimaze()
{
    // if there’s data available, read a packet
    int packetSize = udpMIDI.parsePacket();
    if (packetSize > 0)
    {
        udpMIDI.read(buf_net, MIDIMAZE_BUFFER_SIZE);
        // Send to Atari UART
        fnUartSIO.write(buf_net, packetSize);
#ifdef DEBUG
        Debug_print("MIDI-IN: ");
        util_dump_bytes(buf_net, packetSize);
#endif
    }

    // Read the data until there's a pause in the incoming stream
    if (fnUartSIO.available())
    {
        while (true)
        {
            // Break out of MIDIMaze mode if COMMAND is asserted
            if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
            {
#ifdef DEBUG
                Debug_println("CMD Asserted in LOOP, stopping MIDIMaze");
#endif
                sio_disable_midimaze();
                return;
            }
            if (fnUartSIO.available())
            {
                // Collect bytes read in our buffer
                buf_midi[buf_midi_index] = (char)fnUartSIO.read();
                if (buf_midi_index < MIDIMAZE_BUFFER_SIZE - 1)
                    buf_midi_index++;
            }
            else
            {
                fnSystem.delay_microseconds(MIDIMAZE_PACKET_TIMEOUT);
                if (!fnUartSIO.available())
                    break;
            }
        }

        // Send what we've collected over WiFi
        udpMIDI.beginPacket(midimaze_host_ip, MIDIMAZE_PORT); // remote IP and port
        udpMIDI.write(buf_midi, buf_midi_index);
        udpMIDI.endPacket();

#ifdef DEBUG
        Debug_print("MIDI-OUT: ");
        util_dump_bytes(buf_midi, buf_midi_index);
#endif
        buf_midi_index = 0;
    }
}

void sioMIDIMaze::sio_status()
{
    // Nothing to do here
    return;
}

void sioMIDIMaze::sio_process(uint32_t commanddata, uint8_t checksum)
{
    // Nothing to do here
    return;
}
