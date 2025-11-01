#ifndef NETWORK_H
#define NETWORK_H

#include <memory>
#include <string>
#include <vector>

#include "bus.h"

#include "Protocol.h"
#include "peoples_url_parser.h"
#include "networkStatus.h"
#include "status_error_codes.h"
#include "fnjson.h"

#include "ProtocolParser.h"

/**
 * Number of devices to expose via RS232, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

enum FujiChannelMode {
    NETWORK_PROTOCOL = 0,
    NETWORK_JSON     = 1,
};

class rs232Network : public virtualDevice
{

public:
    /**
     * Constructor
     */
    rs232Network();

    /**
     * Destructor
     */
    virtual ~rs232Network();

#ifdef ESP_PLATFORM
    /**
     * The spinlock for the ESP32 hardware timers. Used for interrupt rate limiting.
     */
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif /* ESP_PLATFORM */

    /**
     * Toggled by the rate limiting timer to indicate that the PROCEED interrupt should
     * be pulsed.
     */
    bool interruptProceed = false;

    /**
     * Called for RS232 Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void rs232_open(FujiTranslationMode mode, uint8_t spec);

    /**
     * Called for RS232 Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     * and stop the receive PROCEED interrupt.
     */
    virtual void rs232_close();

    /**
     * RS232 Read command
     * Read # of bytes from the protocol adapter specified by the aux1/aux2 bytes, into the RX buffer. If we are short
     * fill the rest with nulls and return ERROR.
     *
     * @note It is the channel's responsibility to pad to required length.
     */
    virtual void rs232_read(uint16_t length);

    /**
     * RS232 Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to RS232. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void rs232_write(uint16_t length);

#ifdef OBSOLETE
    /**
     * RS232 Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
     * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
     * with them locally. Then serialize resulting NetworkStatus object to RS232.
     */
    virtual void rs232_setDirection(FujiDirection dir);
#endif /* OBSOLETE */

    /**
     * RS232 Special, called as a default for any other RS232 command not processed by the other rs232_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either rs232_complete()
     * or rs232_error() is called.
     */
    void rs232_status(FujiStatusReq reqType) override;

    /**
     * @brief set channel mode, JSON or PROTOCOL
     */
    virtual void rs232_set_channel_mode(FujiChannelMode chanMode);

    /**
     * @brief Called to set prefix
     */
    virtual void rs232_set_prefix();

    /**
     * @brief Called to get prefix
     */
    virtual void rs232_get_prefix();

    /**
     * @brief called to set login
     */
    virtual void rs232_set_login();

    /**
     * @brief called to set password
     */
    virtual void rs232_set_password();

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    void rs232_poll_interrupt();

    /**
     * Process incoming RS232 command for device 0x7X
     * @param comanddata incoming 4 bytes containing command and aux bytes
     * @param checksum 8 bit checksum
     */
    void rs232_process(FujiBusPacket &packet) override;

    void rs232_seek(uint32_t offset);
    void rs232_tell();

private:
    /**
     * Buffer for holding devicespec
     */
    uint8_t devicespecBuf[256];

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
    NetworkStatus status;

#ifdef ESP_PLATFORM
    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
    esp_timer_handle_t rateTimerHandle = nullptr;
#endif /* ESP_PLATFORM */

    /**
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by RS232 call 0x2C
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
    FujiTranslationMode trans_mode = TRANSLATION_MODE_NONE;

    /**
     * Return value for DSTATS inquiry
     */
    FujiDirection inq_dstats = DIRECTION_INVALID;

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
     * The channel mode for the currently open RS232 device. By default, it is PROTOCOL, which passes
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
    uint16_t json_bytes_remaining = 0;

    /**
     * Instantiate protocol object
     * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
     */
    bool instantiate_protocol();

    /**
     * Create the deviceSpec and fix it for parsing
     */
    void create_devicespec(uint8_t spec);

    /**
     * Create a urlParser from deviceSpec
    */
   void create_url_parser();

#ifdef ESP_PLATFORM
    /**
     * Start the Interrupt rate limiting timer
     */
    void timer_start();

    /**
     * Stop the Interrupt rate limiting timer
     */
    void timer_stop();
#endif /* ESP_PLATFORM */

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
    void processCommaFromDevicespec(unsigned int dev);

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
     */
    bool rs232_read_channel(uint16_t num_bytes);

    /**
     * @brief Perform read of the current JSON channel
     * @param num_bytes Number of bytes to read
     */
    bool rs232_read_channel_json(uint16_t num_bytes);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return TRUE on error, FALSE on success. Used to emit rs232_error or rs232_complete().
     */
    bool rs232_write_channel(uint16_t num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void rs232_status_local(FujiStatusReq reqType);

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void rs232_status_channel();

    /**
     * @brief get JSON status (# of bytes in receive channel)
     */
    bool rs232_status_channel_json(NetworkStatus *ns);

#ifdef OBSOLETE
    /**
     * @brief Do an inquiry to determine whether a protoocol supports a particular command.
     * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
     * or $FF - Command not supported, which should then be used as a DSTATS value by the
     * Atari when making the N: RS232 call.
     */
    void rs232_special_inquiry();
#endif /* OBSOLETE */

    /**
     * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
     * Essentially, call the protocol action
     * and based on the return, signal rs232_complete() or error().
     */
    void rs232_special_00();

    /**
     * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
     * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
     * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
     * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
     */
    void rs232_special_40();

    /**
     * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
     * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
     * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
     * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
     */
    void rs232_special_80();

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void rs232_assert_interrupt();

    /**
     * @brief Perform the inquiry, handle both local and protocol commands.
     * @param inq_cmd the command to check against.
     */
    void do_inquiry(unsigned char inq_cmd);

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void rs232_set_translation(FujiTranslationMode mode);
    /**
     * @brief Parse incoming JSON. (must be in JSON channelMode)
     */
    void rs232_parse_json();

    /**
     * @brief Set JSON query std::string. (must be in JSON channelMode)
     */
    void rs232_set_json_query(FujiTranslationMode mode);

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void rs232_set_timer_rate(int newRate);

    /**
     * @brief perform ->FujiNet commands on protocols that do not use an explicit OPEN channel.
     */
    void rs232_do_idempotent_command_80();

    /**
     * @brief parse URL and instantiate protocol
     */
    void parse_and_instantiate_protocol(uint8_t spec);

};

#endif /* NETWORK_H */
