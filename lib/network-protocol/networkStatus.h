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
        connected=0;
        error=0;
    }
};

#endif /* NETWORKSTATUS_H */
