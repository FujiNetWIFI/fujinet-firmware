#ifndef ADAM_SERIAL_H
#define ADAM_SERIAL_H

#include <cstdint>

#include "bus.h"

#include "fnTcpClient.h"
#include "fnTcpServer.h"

class adamSerial : public virtualDevice
{
    public:

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
    virtual void adamnet_control_ready();
    
    void adamnet_control_send();
};

#endif /* ADAM_SERIAL_H */