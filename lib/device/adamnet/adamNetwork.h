#ifndef NETWORK_H
#define NETWORK_H

#include <memory>
#include <string>

#include "bus.h"

#include "peoples_url_parser.h"

#include "Protocol.h"
#include "network_data.h"

#include "fnjson.h"
#include "fnsgml.h"

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
    void open(const FujiAdamPacket &packet);

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
    void write(const FujiAdamPacket &packet);

    /**
     * ADAM Special, called as a default for any other ADAM command not processed by the other adamnet_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either adamnet_complete()
     * or adamnet_error() is called.
     */
    void status();

    void adamnet_control_send(const FujiAdamPacket &packet) override;
    void adamnet_control_receive() override;

    AdamNetStatus deviceStatus() override;
    std::optional<ByteBuffer> adamnet_control_receive_channel_json();
    std::optional<ByteBuffer> adamnet_control_receive_channel_sgml();
    std::optional<ByteBuffer> adamnet_control_receive_channel_protocol();

    /**
     * @brief Called to set prefix
     */
    void set_prefix(const FujiAdamPacket &packet);

    /**
     * @brief Called to get prefix
     */
    void get_prefix();

    /**
     * @brief called to set login
     */
    void set_login(const FujiAdamPacket &packet);

    /**
     * @brief called to set password
     */
    void set_password(const FujiAdamPacket &packet);

    /**
     * @brief set channel mode
     */
    void channel_mode(const FujiAdamPacket &packet);

    /**
     * @brief parse incoming data
     */
    void json_parse();

    /**
     * @brief JSON Query
     * @param s size of query
     */
    void json_query(const FujiAdamPacket &packet);

    /**
     * @brief parse incoming SGML/HTML/XML
     */
    void sgml_parse();

    /**
     * @brief SGML CSS selector Query
     * @param s size of query
     */
    void sgml_query(const FujiAdamPacket &packet);

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    void adamnet_poll_interrupt();

    /**
     * Process incoming ADAM command for device 0x7X
     * @param b The incoming command byte
     */
    void process_fs(const FujiAdamPacket &packet);
    void process_tcp(const FujiAdamPacket &packet);
    void process_http(const FujiAdamPacket &packet);
    void process_udp(const FujiAdamPacket &packet);

private:
    /**
     * JSON Object
     */
    FNJSON json;

    /**
     * SGML Object (HTML/XML via CSS selector)
     */
    FNSGML sgml;

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
    std::unique_ptr<NetworkProtocol> protocol = nullptr;

    /**
     * Error from the last failed open, reported by get_error() while no
     * protocol is instantiated
     */
    nDevStatus_t err_open = NDEV_STATUS::NOT_CONNECTED;

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
    channelMode_t channelMode = CHANNEL_MODE::PROTOCOL;

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
