#ifndef PCLINK_H
#define PCLINK_H

#include "bus.h"

class sioPCLink : public virtualDevice
{
protected:
    uint8_t status[4];

public:
    sioPCLink();

    // these methods must be implemented
    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    virtual void sio_status() override;

    // public wrapper around protected virtualDevice::sio_ack(), virtualDevice::sio_nak()
    //  to make these protected methotds reachable from sio2bsd/pclink code
    void send_ack_byte(uint8_t  what);

    void mount(int no, const char* fileName);
    void unmount(int no);
};

extern sioPCLink pcLink;

#endif // PCLINK_H

