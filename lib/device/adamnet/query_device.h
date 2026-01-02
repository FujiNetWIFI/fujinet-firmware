#ifndef ADAM_QUERY_H
#define ADAM_QUERY_H

#include <cstdint>

#include "bus.h"

#define ADAMNET_SEARCH_DEVICE_TIMEOUT (220)

#define CONTROL_STATUS   0x10
#define RESPONSE_STATUS  0x80

class adamQueryDevice : public virtualDevice
{
protected:
    virtual void adamnet_control_status() override;
    virtual void adamnet_control_receive();
    virtual void adamnet_control_clr();
    virtual void adamnet_control_ready() override;

    void adamnet_process(uint8_t b) override;
    void shutdown() override;

public:
    adamQueryDevice();
    ~adamQueryDevice();
};

#endif /* ADAM_QUERY_H */
