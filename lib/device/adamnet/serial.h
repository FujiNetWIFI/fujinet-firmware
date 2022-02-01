#ifndef ADAM_SERIAL_H
#define ADAM_SERIAL_H

#include <cstdint>

#include "bus.h"

#include "fnTcpClient.h"
#include "fnTcpServer.h"

class adamSerial : public adamNetDevice
{
    public:

    uint8_t response[16];
    uint16_t response_len=0;
    uint8_t sendbuf[16];
    bool isReady=true;
    bool alreadyDoingSomething=false;
    uint8_t status_msg[4]={0x10,0x00,0x00,0x00};
    fnTcpServer *server;
    fnTcpClient client;
    uint8_t outc;

    /**
     * Constructor
     */
    adamSerial();

    /**
     * Destructor
     */
    virtual ~adamSerial();

    /**
     * Process incoming SIO command for device 0x7X
     * @param b first byte of packet
     */
    virtual void adamnet_process(uint8_t b);

    void command_connect(uint16_t s);
    void command_send(uint16_t s);
    void command_recv();

    /**
     * return adamnet status
     */
    virtual void adamnet_idle();
    virtual void adamnet_response_status();
    
    void adamnet_control_send();
    void adamnet_control_ready();
    void adamnet_control_clr();
};

#endif /* ADAM_SERIAL_H */