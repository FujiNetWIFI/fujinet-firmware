/**
 * Network Status object
 */

#ifndef NETWORKSTATUS_H
#define NETWORKSTATUS_H

#include <cstdint>


class NetworkStatus
{
public:
    NetworkStatus()
    {
        reset();
    }

    /**
     * Number of bytes waiting in RX buffer (0-65535)
     */
    uint16_t rxBytesWaiting;

    /**
     * Not used
     */
    uint8_t connected;

    /**
     * Error code to return to CIO or SIO caller. (1-255)
     */
    uint8_t error;

    /**
     * Reset status
     */
    void reset()
    {
        rxBytesWaiting=0;
        connected=0;
        error=0;
    }

    int checksum()
    {
        return rxBytesWaiting+connected+error;
    }
};

#endif /* NETWORKSTATUS_H */