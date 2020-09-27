#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include "sio.h"
#include "EdUrlParser.h"
#include "networkProtocol.h"
#include "networkStatus.h"

/**
 * Number of devices to expose via SIO, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8


/**
 * The size of rx and tx buffers
 */
#ifdef BOARD_HAS_PSRAM
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#else
#define INPUT_BUFFER_SIZE 8192
#define OUTPUT_BUFFER_SIZE 2048
#endif

/**
 * Attempted to use connection while not open
 */
#define NETWORK_ERROR_NOT_CONNECTED 133

/**
 * A fatal error
 */
#define NETWORK_ERROR_GENERAL 144

/**
 * An invalid devicespec was given
 */
#define NETWORK_ERROR_INVALID_DEVICESPEC 165

/**
 * A connection was either refused or not possible
 */
#define NETWORK_ERROR_CONNECTION_REFUSED 170

class sioNetwork : public sioDevice
{

public:
    virtual void sio_open();
    virtual void sio_close();
    virtual void sio_read();
    virtual void sio_write();
    virtual void sio_special();
    virtual void sio_status();

    virtual void sio_process(uint32_t commanddata, uint8_t checksum);

private:

    /**
     * The Receive buffer for this N: device
     */
    uint8_t *rx_buf = nullptr;

    /**
     * The transmit buffer for this N: device
     */
    uint8_t *tx_buf = nullptr;

    /**
     * The EdUrlParser object used to hold/process a URL
     */
    EdUrlParser *urlParser = nullptr;

    /**
     * Instance of currently open network protocol
     */
    networkProtocol *protocol = nullptr;

    /**
     * Network Status object
     */
    NetworkStatus status;

    /**
     * Allocate rx and tx buffers
     * @return bool TRUE if ok, FALSE if in error.
     */
    bool allocate_buffers();

    /**
     * Free the rx and tx buffers
     */
    void free_buffers();

    /**
     * Instantiate protocol object
     * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
     */
    bool open_protocol();

};

#endif /* NETWORK_H */
