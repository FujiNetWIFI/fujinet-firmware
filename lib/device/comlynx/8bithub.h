#ifndef HUB8BIT_H
#define HUB8BIT_H

#include "bus.h"

#include "fnUDP.h"

#define UDPSTREAM_BUFFER_SIZE 8192
#define UDPSTREAM_PACKET_TIMEOUT 5000
#define MIDI_PORT 5004

typedef struct
{
    bool active = false;
    ip_addr_t ip;
    int inPort;
    int outPort;
}  udpSlot;

class lynx8bithub : public virtualDevice
{
private:
    systemBus *_comlynx_bus;

    void comlynx_8bithub_udpopen(uint8_t len);

    void comlynx_process(uint8_t b) override;

public:
    bool hubActive = false; // If we are in hub mode or not

    void comlynx_8bithub_enable();
    void comlynx_8bithub_disable();
    void comlynx_handle_8bithub();

};

#endif