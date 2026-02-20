/**
 * Network Status object
 */

#ifndef NETWORKSTATUS_H
#define NETWORKSTATUS_H

#include "status_error_codes.h"
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
    nDevStatus_t error;

    /**
     * Reset status
     */
    void reset()
    {
        connected=0;
        error=NDEV_STATUS::SUCCESS;
    }
};

#endif /* NETWORKSTATUS_H */
