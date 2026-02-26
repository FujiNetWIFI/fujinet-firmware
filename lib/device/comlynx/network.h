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
 * Number of devices to expose via LYNX, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/*
 * Size of the serial packet we can send to Lynx (determined mainly by the Lynx RX buffer)
 */
#define SERIAL_PACKET_SIZE 256

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class lynxNetwork : public virtualDevice
{

public:
    /**
     * Constructor
     */
    lynxNetwork();

    /**
     * Destructor
     */
    virtual ~lynxNetwork();

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
     * Called for LYNX Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    void open(unsigned short s);

    /**
     * Called for LYNX Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    void close();


    /**
     * LYNX Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to LYNX. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    void write(uint16_t num_bytes);

    /**
     * LYNX Special, called as a default for any other LYNX command not processed by the other comlynx_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either comlynx_complete()
     * or comlynx_error() is called.
     */
    void status();

    void read();
    void read_channel();
    void read_channel_json();
    void read_channel_protocol();

    /**
     * @brief Called to set prefix
     */
    void set_prefix(unsigned short len);

    /**
     * @brief Called to get prefix
     */
    void get_prefix();

    /**
     * @brief called to set login
     */
    void set_login(uint16_t len);

    /**
     * @brief called to set password
     */
    void set_password(uint16_t len);

    /**
     * @brief set channel mode
     */
    void set_channel_mode();

    /**
     * @brief parse incoming data
     */
    void json_parse();

    /**
     * @brief JSON Query
     * @param s size of query
     */
    void json_query(unsigned short len);

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    //void comlynx_poll_interrupt();

    /**
     * Process incoming LYNX command for device 0x7X
     * @param b The incoming command byte
     */
    void comlynx_process() override;
    void process_fs(fujiCommandID_t cmd, unsigned pkt_len);
    void process_tcp(fujiCommandID_t cmd);
    void process_http(fujiCommandID_t cmd);
    void process_udp(fujiCommandID_t cmd);

private:
    /**
     * LynxNet Response Buffer
     */
    uint8_t response[1024];

    /**
     * LynxNet Response Length
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
     * Error number, if status.bits.client_error is set.
     */
    nDevStatus_t err = NDEV_STATUS::SUCCESS;

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
    esp_timer_handle_t rateTimerHandle = nullptr;

    /**
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by LYNX call 0x2C
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
     * The channel mode for the currently open LYNX device. By default, it is PROTOCOL, which passes
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
     * @return PROTOCOL_ERROR::UNSPECIFIED on error, PROTOCOL_ERROR::NONE on success. Passed directly to bus_to_computer().
     */
    protocolError_t read_channel(unsigned short num_bytes);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return PROTOCOL_ERROR::UNSPECIFIED on error, PROTOCOL_ERROR::NONE on success. Used to emit comlynx_error or comlynx_complete().
     */
    protocolError_t write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void comlynx_status_local();

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void comlynx_status_channel();

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void comlynx_assert_interrupt();

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void comlynx_set_translation();

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void comlynx_set_timer_rate();

    /**
     * @brief parse URL and instantiate protocol
     * @param db pointer to devicespecbuf 256 chars
     */
    void parse_and_instantiate_protocol(std::string d);

    //void transaction_continue(bool expectMoreData) override {};
    void transaction_complete();
    void transaction_error();
    bool transaction_get(void *data, size_t len);
    void transaction_put(const void *data, size_t len, bool err=false);
};

#endif /* NETWORK_H */
