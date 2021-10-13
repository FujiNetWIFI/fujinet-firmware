#ifndef ADAM_NETWORK_H
#define ADAM_NETWORK_H

#include <string>
#include <vector>
#include "bus.h"
#include "EdUrlParser.h"
#include "driver/timer.h"

class adamNetwork : public adamNetDevice
{
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

    /**
     * return adamnet status
     */
    virtual void adamnet_status();
            
};

#endif /* ADAM_NETWORK_H */