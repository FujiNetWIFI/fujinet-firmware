#ifndef ADAM_QUERY_H
#define ADAM_QUERY_H

#include <string.h>
#include <queue>

#include "bus.h"
#include "fnTcpServer.h"

#define ADAMNET_KEYBOARD 0x01
#define ADAMNET_PRINTER  0x02

#define CONTROL_STATUS   0x10
#define RESPONSE_STATUS  0x80


class adamQueryDevice : public adamNetDevice
{
protected:
protected:
    // SIO THINGS
    
    virtual void adamnet_control_status();
    virtual void adamnet_control_receive();
    virtual void adamnet_control_clr();
    virtual void adamnet_control_ready();

    void adamnet_process(uint8_t b) override;
    void shutdown() override;

public:

    bool adamDeviceExists(uint8_t device);

    adamQueryDevice();
    ~adamQueryDevice();

};

#endif /* ADAM_QUERY_H */