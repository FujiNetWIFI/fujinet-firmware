#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include <vector>
#include "sio.h"
#include "EdUrlParser.h"
#include "../network-protocol/networkProtocol.h"
#include "networkStatus.h"
#include "driver/timer.h"

/**
 * Number of devices to expose via SIO, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535

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

/**
 * Could not allocate buffers
 */
#define NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS 255

class sioNetwork : public sioDevice
{

public:
    /**
     * Constructor
     */
    sioNetwork();

    /**
     * Destructor
     */
    virtual ~sioNetwork();

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
     *  
     * @note It is the channel's responsibility to pad to required length.
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
     * Check to see if PROCEED needs to be asserted.
     */
    void sio_assert_interrupts();

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
    uint8_t *receiveBuffer = nullptr;

    /**
     * The transmit buffer for this N: device
     */
    uint8_t *transmitBuffer = nullptr;

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
     * The AUX1 value used for OPEN.
     */
    uint8_t open_aux1;

    /**
     * The AUX2 value used for OPEN.
     */
    uint8_t open_aux2;

    /**
     * The Translation mode ORed into AUX2 for READ/WRITE/STATUS operations.
     * 0 = No Translation, 1 = CR<->EOL (Macintosh), 2 = LF<->EOL (UNIX), 3 = CR/LF<->EOL (PC/Windows)
     */
    uint8_t trans_aux2;

    /**
     * The channel mode for the currently open SIO device. By default, it is PROTOCOL, which passes
     * read/write/status commands to the protocol. Otherwise, it's a special mode, e.g. to pass to
     * the JSON or XML parsers.
     * 
     * @enum PROTOCOL Send to protocol
     * @enum JSON Send to JSON parser.
     */
    enum _channel_mode
    {
        PROTOCOL,
        JSON
    } channelMode;

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
    bool instantiate_protocol();

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

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return TRUE on error, FALSE on success. Passed directly to sio_to_computer().
     */
    bool sio_read_channel(unsigned short num_bytes);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return TRUE on error, FALSE on success. Used to emit sio_error or sio_complete().
     */
    bool sio_write_channel(unsigned short num_bytes);
};

#endif /* NETWORK_H */
