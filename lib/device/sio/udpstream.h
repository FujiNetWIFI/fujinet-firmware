#ifndef UDPSTREAM_H
#define UDPSTREAM_H

#ifdef ESP_PLATFORM
#include <driver/ledc.h>
#include "sdkconfig.h"
#endif

#include "bus.h"

#include "fnUDP.h"

#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_1_BIT

#ifdef ESP_PLATFORM
#ifdef CONFIG_IDF_TARGET_ESP32
   #define LEDC_ESP32XX_HIGH_SPEED LEDC_HIGH_SPEED_MODE
#else
   #define LEDC_ESP32XX_HIGH_SPEED LEDC_LOW_SPEED_MODE
#endif
#endif

#define UDPSTREAM_BUFFER_SIZE 8192
#define UDPSTREAM_PACKET_TIMEOUT 5000
#define UDPSTREAM_KEEPALIVE_TIMEOUT 250000      // MIDI Keep Alive is 300ms
#define MIDI_PORT 5004
#define MIDI_BAUDRATE 31250

class sioUDPStream : public virtualDevice
{
private:
    fnUDP udpStream;

    uint8_t buf_net[UDPSTREAM_BUFFER_SIZE];
    uint8_t buf_stream[UDPSTREAM_BUFFER_SIZE];

    unsigned int buf_stream_index=0;

    uint16_t packet_seq = 0;
#ifdef ESP_PLATFORM
    uint32_t start = (uint32_t)esp_timer_get_time(); // Keep alive timer
#endif
    void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool udpstreamActive = false; // If we are in udpstream mode or not
    bool udpstreamIsServer = false; // If we are connecting to a server
    in_addr_t udpstream_host_ip = IPADDR_NONE;
    int udpstream_port;

    void sio_enable_udpstream();  // setup udpstream
    void sio_disable_udpstream(); // stop udpstream
    void sio_handle_udpstream();  // Handle incoming & outgoing data for udpstream
};

#endif
