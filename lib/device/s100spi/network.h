#ifndef NETWORK_H
#define NETWORK_H

#include <esp_timer.h>

#include <string>

#include "bus.h"

#include "EdUrlParser.h"

#include "Protocol.h"


/**
 * Number of devices to expose via s100spi, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class s100spiNetwork : public virtualDevice
{

public:
    /**
     * Constructor
     */
    s100spiNetwork();

    /**
     * Destructor
     */
    virtual ~s100spiNetwork();

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
     * Called for s100spi Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void open(unsigned short s);

    /**
     * Called for s100spi Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    virtual void close();


    /**
     * s100spi Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to s100spi. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void write(uint16_t num_bytes);

    /**
     * s100spi Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
     * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
     * with them locally. Then serialize resulting NetworkStatus object to s100spi.
     */
    virtual void s100spiNetwork_special();

    /**
     * s100spi Special, called as a default for any other s100spi command not processed by the other s100spinet_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either s100spinet_complete()
     * or s100spinet_error() is called.
     */
    virtual void status();

    virtual void s100spi_control_ack();
    virtual void s100spi_control_clr();
    virtual void s100spi_control_receive();
    virtual void s100spi_control_receive_channel();
    virtual void s100spi_control_send();

    virtual void s100spi_response_status();
    virtual void s100spi_response_send();

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
    virtual void set_login(uint16_t s);

    /**
     * @brief called to set password
     */
    virtual void set_password(uint16_t s);

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    void s100spinet_poll_interrupt();

    /**
     * Process incoming s100spi command for device 0x7X
     * @param b The incoming command byte
     */
    virtual void s100spi_process(uint8_t b);

    virtual void del(uint16_t s);
    virtual void rename(uint16_t s);
    virtual void mkdir(uint16_t s);


private:
    /**
     * s100spiNet Response Buffer
     */
    uint8_t response[1024];

    /**
     * s100spiNet Response Length
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
     * The EdUrlParser object used to hold/process a URL
     */
    EdUrlParser *urlParser = nullptr;

    /**
     * Instance of currently open network protocol
     */
    NetworkProtocol *protocol = nullptr;

    /**
     * Network Status object
     */
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
    uint8_t err; 

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
    esp_timer_handle_t rateTimerHandle = nullptr;

    /**
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by s100spi call 0x2C
     */
    std::string prefix;

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
     * Return value for DSTATS inquiry
     */
    uint8_t inq_dstats = 0xFF;

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
     * The channel mode for the currently open s100spi device. By default, it is PROTOCOL, which passes
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
     * of s100spiNetwork.
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
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool read_channel(unsigned short num_bytes);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return TRUE on error, FALSE on success. Used to emit s100spinet_error or s100spinet_complete().
     */
    bool s100spiNetwork_write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void s100spiNetwork_status_local();

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void s100spiNetwork_status_channel();

    /**
     * @brief Do an inquiry to determine whether a protoocol supports a particular command.
     * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
     * or $FF - Command not supported, which should then be used as a DSTATS value by the
     * Atari when making the N: s100spi call.
     */
    void s100spiNetwork_special_inquiry();

    /**
     * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
     * Essentially, call the protocol action
     * and based on the return, signal s100spinet_complete() or error().
     */
    void s100spiNetwork_special_00();

    /**
     * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
     * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
     * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
     * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
     */
    void s100spiNetwork_special_40();

    /**
     * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
     * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
     * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
     * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
     */
    void s100spiNetwork_special_80();

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void s100spiNetwork_assert_interrupt();

    /**
     * @brief Perform the inquiry, handle both local and protocol commands.
     * @param inq_cmd the command to check against.
     */
    void do_inquiry(unsigned char inq_cmd);

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void s100spiNetwork_set_translation();

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void s100spiNetwork_set_timer_rate();

    /**
     * @brief perform ->FujiNet commands on protocols that do not use an explicit OPEN channel.
     */
    void s100spiNetwork_do_idempotent_command_80();

    /**
     * @brief parse URL and instantiate protocol
     * @param db pointer to devicespecbuf 256 chars
     */
    void parse_and_instantiate_protocol(std::string d);
};

#endif /* NETWORK_H */
