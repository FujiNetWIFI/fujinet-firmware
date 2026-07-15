#ifndef NETWORK_H
#define NETWORK_H

#include <memory>
#include <string>

#include "bus.h"

#include "peoples_url_parser.h"

#include "Protocol.h"

#include "fnjson.h"

#include "ProtocolParser.h"

/**
 * Number of devices to expose via ADAM, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class adamNetwork : public virtualDevice
{

public:
    /**
     * Constructor
     */
    adamNetwork();

    /**
     * Destructor
     */
    virtual ~adamNetwork();

    /**
     * Toggled by the rate limiting timer to indicate that the PROCEED interrupt should
     * be pulsed.
     */
    bool interruptProceed = false;

    /**
     * @brief called to return the extended error number from a protocol adapter
     */
    void get_error();

    /**
     * Called for ADAM Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    void open(unsigned short s);

    /**
     * Called for ADAM Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    void close();


    /**
     * ADAM Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to ADAM. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    void write(uint16_t num_bytes);

    /**
     * ADAM Special, called as a default for any other ADAM command not processed by the other adamnet_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either adamnet_complete()
     * or adamnet_error() is called.
     */
    void status();

    void adamnet_control_ack();
    void adamnet_control_clr();
    void adamnet_control_receive();
    void adamnet_control_receive_channel_json();
    void adamnet_control_receive_channel_protocol();
    void adamnet_control_send();

    void adamnet_response_status() override;
    void adamnet_response_send();

    /**
     * @brief Called to set prefix
     */
    void set_prefix(unsigned short s);

    /**
     * @brief Called to get prefix
     */
    void get_prefix();

    /**
     * @brief called to set login
     */
    void set_login(uint16_t s);

    /**
     * @brief called to set password
     */
    void set_password(uint16_t s);

    /**
     * @brief set channel mode
     */
    void channel_mode();

    /**
     * @brief parse incoming data
     */
    void json_parse();

    /**
     * @brief JSON Query
     * @param s size of query
     */
    void json_query(unsigned short s);

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    void adamnet_poll_interrupt();

    /**
     * Process incoming ADAM command for device 0x7X
     * @param b The incoming command byte
     */
    void adamnet_process(uint8_t b) override;
    void process_fs(fujiCommandID_t cmd, unsigned pkt_len);
    void process_tcp(fujiCommandID_t cmd);
    void process_http(fujiCommandID_t cmd);
    void process_udp(fujiCommandID_t cmd);

private:
    /**
     * AdamNet Response Buffer
     */
    uint8_t response[1024];

    /**
     * AdamNet Response Length
     */
    uint16_t response_len=0;

    /**
     * JSON Object
     */
    FNJSON json;

    /**
     * Has JSON been sent via CLR?
     */
    bool jsonRecvd = false;

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
     * Error from the last failed open, reported by get_error() while no
     * protocol is instantiated
     */
    nDevStatus_t err_open = NDEV_STATUS::NOT_CONNECTED;

    /**
     * @brief Factory that creates protocol from urls
    */
    ProtocolParser *protocolParser = nullptr;

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
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by ADAM call 0x2C
     */
    std::string prefix;

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
     * The channel mode for the currently open ADAM device. By default, it is PROTOCOL, which passes
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
     * Create the deviceSpec and fix it for parsing
     */
    void create_devicespec(std::string d, bool is_dir);

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
     * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Passed directly to bus_to_computer().
     */
    fujiError_t read_channel(unsigned short num_bytes);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Used to emit adamnet_error or adamnet_complete().
     */
    fujiError_t adamnet_write_channel(unsigned short num_bytes);

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void adamnet_assert_interrupt();

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void adamnet_set_translation();

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void adamnet_set_timer_rate();

    /**
     * @brief parse URL and instantiate protocol
     * @param db pointer to devicespecbuf 256 chars
     */
    void parse_and_instantiate_protocol(std::string d, bool is_dir);
};

#endif /* NETWORK_H */
