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
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
    //ledc_timer.clock_divider = ?;
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
    Debug_println("MIDIMAZE Mode enabled");
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
    udpMIDI.read(buf1, MIDIMAZE_BUFFER_SIZE);
    // now send to UART:
    fnUartSIO.write(buf1, packetSize);
#ifdef DEBUG
    Debug_print("MIDI-IN: ");
    Debug_println((char*)buf1);
#endif
    fnUartSIO.flush();
  }

  if (fnUartSIO.available()) {
    // read the data until pause:
    if (fnSystem.digital_read(PIN_MTR) == DIGI_LOW || fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
    {
      fnUartSIO.read(); // Toss the data if motor or command is asserted
    }
    else
    {
      while (1)
      {
        if (fnUartSIO.available())
        {
          buf2[i2] = (char)fnUartSIO.read(); // read char from UART
#ifdef DEBUG
          Debug_print("MIDI-OUT: ");
          Debug_println(buf2[i2]);
#endif
          if (i2 < MIDIMAZE_BUFFER_SIZE - 1) i2++;
        }
        else
        {
          fnSystem.delay_microseconds(MIDIMAZE_PACKET_TIMEOUT);
          if (!fnUartSIO.available())
            break;
        }
      }

      // now send to WiFi:
      udpMIDI.beginPacket(midimaze_host_ip, MIDIMAZE_PORT); // remote IP and port
      udpMIDI.write(buf2, i2);
      udpMIDI.endPacket();
      i2 = 0;
    }
  }
}

void sioMIDIMaze::sio_status()
{
    // Nothing to do here
}

void sioMIDIMaze::sio_process(uint32_t commanddata, uint8_t checksum)
{
    // Nothing to do here
}
