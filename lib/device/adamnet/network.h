#ifndef ADAM_NETWORK_H
#define ADAM_NETWORK_H

#include <string>
#include <vector>
#include "bus.h"
#include "EdUrlParser.h"
#include "driver/timer.h"
#include "fnTcpClient.h"

class adamNetwork : public adamNetDevice
{
    public:

    uint8_t response[1024];
    uint16_t response_len=0;
    bool isReady=true;
    bool alreadyDoingSomething=false;

    fnTcpClient client;

    /**
     * Constructor
     */
    adamNetwork();

    /**
     * Destructor
     */
    virtual ~adamNetwork();

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
    virtual void adamnet_control_status();
    
    void adamnet_control_send();
    void adamnet_control_ready();
    void adamnet_control_clr();
};

#endif /* ADAM_NETWORK_H */