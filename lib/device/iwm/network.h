#ifndef NETWORK_H
#define NETWORK_H

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "bus.h"
#include "fnjson.h"
#include "network_data.h"
#include "peoples_url_parser.h"
#include "Protocol.h"
// #include "ProtocolParser.h"


/**
 * Number of devices to expose via APPLE2
 */
#define NUM_DEVICES 4

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class iwmNetwork : public virtualDevice
{

public:

    /**
     * Command frame for protocol adapter
     */
    cmdFrame_t cmdFrame;

    /**
     * Constructor
     */
    iwmNetwork();

    /**
     * Destructor
     */
    virtual ~iwmNetwork();

    /**
     * The spinlock for the ESP32 hardware timers. Used for interrupt rate limiting.
     */
#ifdef ESP_PLATFORM // OS
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

    /**
     * Toggled by the rate limiting timer to indicate that the PROCEED interrupt should
     * be pulsed.
     */
    bool interruptProceed = false;

    /**
     * Called for iwm Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void open();

    /**
     * Called for iwm Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    virtual void close();

    /**
     * Write to Network Socket 'W'
     */
    void net_write();

    /**
     * Read from Network Socket 'R'
     */
    void net_read();

    /**
     * iwm Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to iwm. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void write(){};

    /**
     * iwm Special, called as a default for any other iwm command not processed by the other iwmnet_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either iwmnet_complete()
     * or iwmnet_error() is called.
     */
    virtual void status();

    void process(iwm_decoded_cmd_t cmd) override;

    void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    void iwm_open(iwm_decoded_cmd_t cmd) override;
    void iwm_close(iwm_decoded_cmd_t cmd) override;
    void iwm_read(iwm_decoded_cmd_t cmd) override;
    void iwm_write(iwm_decoded_cmd_t cmd) override;
    void iwm_status(iwm_decoded_cmd_t cmd) override;
    void shutdown() override;
    void send_status_reply_packet() override;
    void send_extended_status_reply_packet() override{};
    void send_status_dib_reply_packet() override;
    void send_extended_status_dib_reply_packet() override{};

    /**
     * @brief Called to set prefix
     */
    virtual void set_prefix();

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
    void json_query(iwm_decoded_cmd_t cmd);

    virtual void del();
    virtual void rename();
    virtual void mkdir();

    std::unordered_map<uint8_t, NetworkData> network_data_map;
    uint8_t current_network_unit = 1;

private:

    // /**
    //  * JSON Object
    //  */
    // FNJSON json;

    // /**
    //  * The Receive buffer for this N: device
    //  */
    // std::string *receiveBuffer = nullptr;

    // /**
    //  * The transmit buffer for this N: device
    //  */
    // std::string *transmitBuffer = nullptr;

    // /**
    //  * The special buffer for this N: device
    //  */
    // std::string *specialBuffer = nullptr;

    // /**
    //  * The PeoplesUrlParser object used to hold/process a URL
    //  */
    // std::unique_ptr<PeoplesUrlParser> urlParser = nullptr;

    // /**
    //  * Instance of currently open network protocol
    //  */
    // NetworkProtocol *protocol = nullptr;

    // /**
    //  * @brief Factory that creates protocol from urls
    // */
    // ProtocolParser *protocolParser = nullptr;

    /**
     * Error number when there's an ... error!
     */
    uint8_t err = 0;

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
#ifdef ESP_PLATFORM // OS
    esp_timer_handle_t rateTimerHandle = nullptr;
#endif

    // /**
    //  * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
    //  */
    // std::string deviceSpec;

    // /**
    //  * The currently set Prefix for this N: device, set by iwm call 0x2C
    //  */
    // std::string prefix;

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
    // std::string login;

    /**
     * The password to use for a protocol action
     */
    // std::string password;

    /**
     * Timer Rate for interrupt timer
     */
    int timerRate = 100;

    /**
     * The channel mode for the currently open iwm device. By default, it is PROTOCOL, which passes
     * read/write/status commands to the protocol. Otherwise, it's a special mode, e.g. to pass to
     * the JSON or XML parsers.
     *
     * @enum PROTOCOL Send to protocol
     * @enum JSON Send to JSON parser.
     */
    // enum _channel_mode
    // {
    //     PROTOCOL,
    //     JSON
    // } channelMode;

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
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool read_channel(unsigned short num_bytes, iwm_decoded_cmd_t cmd);

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool read_channel_json(unsigned short num_bytes, iwm_decoded_cmd_t cmd);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return TRUE on error, FALSE on success. Used to emit iwmnet_error or iwmnet_complete().
     */
    bool write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void iwmnet_status_local();

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void iwmnet_status_channel();

    /**
     * @brief Do an inquiry to determine whether a protoocol supports a particular command.
     * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
     * or $FF - Command not supported, which should then be used as a DSTATS value by the
     * Atari when making the N: iwm call.
     */
    void iwmnet_special_inquiry();

    /**
     * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
     * Essentially, call the protocol action
     * and based on the return, signal iwmnet_complete() or error().
     */
    void special_00();

    /**
     * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
     * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
     * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
     * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
     */
    void special_40();

    /**
     * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
     * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
     * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
     * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
     */
    void special_80();

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void iwmnet_assert_interrupt();

    /**
     * @brief Perform the inquiry, handle both local and protocol commands.
     * @param inq_cmd the command to check against.
     */
    void do_inquiry(fujiCommandID_t inq_cmd);

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void iwmnet_set_translation();

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void iwmnet_set_timer_rate();

    /**
     * @brief parse URL and instantiate protocol
     * @param db pointer to devicespecbuf 256 chars
     */
    void parse_and_instantiate_protocol(std::string d);
};

#endif /* NETWORK_H */
