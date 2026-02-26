#ifndef NETWORK_H
#define NETWORK_H

#include <esp_timer.h>
#include <memory>
#include <string>

#include "bus.h"

#include "peoples_url_parser.h"

#include "Protocol.h"
#include "fnjson.h"

#include "ProtocolParser.h"

/**
 * Number of devices to expose via H89, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

#define USERNAME_BUFFER_SIZE 256
#define PASSWORD_BUFFER_SIZE 256


class H89Network : public virtualDevice
{

public:
    /**
     * Constructor
     */
    H89Network();

    /**
     * Destructor
     */
    virtual ~H89Network();

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
     * Called for H89 Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void open();

    /**
     * Called for H89 Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    virtual void close();


    /**
     * H89 Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to H89. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void write();

    virtual void read();

    /**
     * H89 Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
     * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
     * with them locally. Then serialize resulting NetworkStatus object to H89.
     */
    virtual void status();

    /**
     * @brief Called to set prefix
     */
    virtual void set_prefix(unsigned short s);

    /**
     * @brief Called to get prefix
     */
    virtual void get_prefix();

    /**
     * @brief called to set login
     */
    virtual void set_login();

    /**
     * @brief called to set password
     */
    virtual void set_password();

    /**
     * Process incoming H89 command for device 0x7X
     * @param b The incoming command byte
     */
    virtual void process(uint32_t commanddata, uint8_t checksum) override;

private:
    /**
     * H89Net Response Buffer
     */
    uint8_t response[1024];

    /**
     * H89Net Response Length
     */
    uint16_t response_len=0;

    /**
     * The Receive buffer for this N: device
     */
    std::string *receiveBuffer = nullptr;

    /**
     * The transmit buffer for this N: device
     */
    std::string *transmitBuffer = nullptr;

    /**
     * The special buffer for this N: device
     */
    std::string *specialBuffer = nullptr;

    /**
     * The PeoplesUrlParser object used to hold/process a URL
     */
    std::unique_ptr<PeoplesUrlParser> urlParser = nullptr;

    /**
     * Instance of currently open network protocol
     */
    NetworkProtocol *protocol = nullptr;

    /**
     * @brief Factory that creates protocol from urls
    */
    ProtocolParser *protocolParser = nullptr;

    /**
     * Network Status object
     */
    NetworkStatus network_status;
    union _status
    {
        struct _statusbits
        {
            bool client_data_available : 1;
            bool client_connected : 1;
            bool client_error : 1;
            bool server_connection_available : 1;
            bool server_error : 1;
        } bits;
        unsigned char byte;
    } statusByte;

    /**
     * Error number, if status.bits.client_error is set.
     */
    uint8_t err = 0;

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
    esp_timer_handle_t rateTimerHandle = nullptr;

    /**
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by H89 call 0x2C
     */
    std::string prefix;

    /**
     * The AUX1 value used for OPEN.
     */
    uint8_t open_aux1 = 0;

    /**
     * The AUX2 value used for OPEN.
     */
    uint8_t open_aux2 = 0;

    /**
     * The Translation mode ORed into AUX2 for READ/WRITE/STATUS operations.
     * 0 = No Translation, 1 = CR<->EOL (Macintosh), 2 = LF<->EOL (UNIX), 3 = CR/LF<->EOL (PC/Windows)
     */
    uint8_t trans_aux2 = 0;

    /**
     * Return value for DSTATS inquiry
     */
    AtariSIODirection inq_dstats = SIO_DIRECTION_INVALID;

    /**
     * The login to use for a protocol action
     */
    std::string login;

    /**
     * The password to use for a protocol action
     */
    std::string password;

    /**
     * Timer Rate for interrupt timer
     */
    int timerRate = 100;

    /**
     * The channel mode for the currently open H89 device. By default, it is PROTOCOL, which passes
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
     * The current receive state, are we sending channel or status data?
     */
    enum _receive_mode
    {
        CHANNEL,
        STATUS
    } receiveMode = CHANNEL;

    /**
     * saved NetworkStatus items
     */
    unsigned char reservedSave = 0;
    unsigned char errorSave = 1;

    /**
     * The fnJSON parser wrapper object
     */
    FNJSON json;

    /**
     * Bytes sent of current JSON query object.
     */
    unsigned short json_bytes_remaining = 0;

    /**
     * Instantiate protocol object
     * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
     */
    bool instantiate_protocol();

    /**
     * Create the deviceSpec and fix it for parsing
     */
    void create_devicespec(std::string d);

    /**
     * Create a urlParser from deviceSpec
    */
   void create_url_parser();

    /**
     * Start the Interrupt rate limiting timer
     */
    void timer_start();

    /**
     * Stop the Interrupt rate limiting timer
     */
    void timer_stop();

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
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool read_channel(unsigned short num_bytes);


    /**
     * @brief Perform read of the current JSON channel
     * @param num_bytes Number of bytes to read
     */
    bool read_channel_json(unsigned short num_bytes);


    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return TRUE on error, FALSE on success. Used to emit H89net_error or H89net_complete().
     */
    bool write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void status_local();

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void status_channel();

    /**
     * @brief get JSON status (# of bytes in receive channel)
     */
    bool status_channel_json(NetworkStatus *ns);

    /**
     * @brief Parse incoming JSON. (must be in JSON channelMode)
     */
    void H89_parse_json();

    /**
     * @brief Set JSON query string. (must be in JSON channelMode)
     */
    void H89_set_json_query();

    /**
     * @brief set channel mode, JSON or PROTOCOL
     */
    virtual void H89_set_channel_mode();

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void assert_interrupt();

    /**
     * @brief Perform the inquiry, handle both local and protocol commands.
     * @param inq_cmd the command to check against.
     */
    void do_inquiry(fujiCommandID_t inq_cmd);

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void set_translation();

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void set_timer_rate();


    /**
     * @brief parse URL and instantiate protocol
     * @param db pointer to devicespecbuf 256 chars
     */
    void parse_and_instantiate_protocol(std::string d);
};

#endif /* NETWORK_H */
