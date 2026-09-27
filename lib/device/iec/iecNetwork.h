#ifndef IECNETWORK_H
#define IECNETWORK_H

#include "NDevice.h"

class iecNetwork : public NDevice
{
public:
    /**
     * @brief CTOR
     */
    iecNetwork(uint8_t devnr) {
        m_devnr = devnr;
    }

    /**
     * @brief DTOR
     */
    virtual ~iecNetwork() {}

protected:
    void fujidev_write(const FUJI_COMMAND_PACKET &packet) override;
};

#endif /* IECNETWORK_H */
