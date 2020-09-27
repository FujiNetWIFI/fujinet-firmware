#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include "sio.h"
#include "EdUrlParser.h"
#include "networkProtocol.h"
#include "networkStatus.h"
#include "driver/timer.h"

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

    /**
     * The spinlock for the ESP32 hardware timers. Used for interrupt rate limiting.
     */
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

    /**
     * Toggled by the rate limiting timer to indicate that the PROCEED interrupt should
     * be pulsed.
     */
    bool interruptProceed = false;

    /**
     * Called for SIO Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void sio_open();

    /**
     * Called for SIO Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    virtual void sio_close();

    /**
     * SIO Read command
     * Read # of bytes from the protocol adapter specified by the aux1/aux2 bytes, into the RX buffer. If we are short
     * fill the rest with nulls and return ERROR.
     */
    virtual void sio_read();

    /**
     * SIO Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to SIO. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void sio_write();

    /**
     * SIO Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
     * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
     * with them locally. Then serialize resulting NetworkStatus object to SIO.
     */
    virtual void sio_special();

    /**
     * SIO Special, called as a default for any other SIO command not processed by the other sio_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either sio_complete()
     * or sio_error() is called.
     */
    virtual void sio_status();

    /**
     * Process incoming SIO command for device 0x7X
     * @param comanddata incoming 4 bytes containing command and aux bytes
     * @param checksum 8 bit checksum
     */
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
     * ESP timer handle for the Interrupt rate limiting timer
     */
    esp_timer_handle_t rateTimerHandle = nullptr;

    /**
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by SIO call 0x2C
     */
    string prefix;

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

    /**
     * Start the Interrupt rate limiting timer
     */
    void timer_start();

    /**
     * Stop the Interrupt rate limiting timer
     */
    void timer_stop();

    /**
     * Is this a valid URL? (used to generate ERROR 165)
     */
    bool isValidURL(EdUrlParser *url);

    /**
     * Preprocess a URL given aux1 open mode. This is used to work around various assumptions that different
     * disk utility packages do when opening a device, such as adding wildcards for directory opens. 
     * 
     * The resulting URL is then sent into EdURLParser to get our URLParser object which is used in the rest
     * of sioNetwork.
     * 
     * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
     */
    bool parseURL();

    /**
     * We were passed a COPY arg from DOS 2. This is complex, because we need to parse the comma,
     * and figure out one of three states:
     * 
     * (1) we were passed D1:FOO.TXT,N:FOO.TXT, the second arg is ours.
     * (2) we were passed N:FOO.TXT,D1:FOO.TXT, the first arg is ours.
     * (3) we were passed N1:FOO.TXT,N2:FOO.TXT, get whichever one corresponds to our device ID.
     * 
     * DeviceSpec will be transformed to only contain the relevant part of the deviceSpec, sans comma.
     */
    void processCommaFromDevicespec();
};

#endif /* NETWORK_H */
