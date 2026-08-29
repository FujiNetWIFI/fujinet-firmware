#ifndef NETWORK_H
#define NETWORK_H

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif

#include <string>
#include <memory>
#include <vector>

#include "bus.h"

#include "Protocol.h"
#include "peoples_url_parser.h"
#include "networkStatus.h"
#include "status_error_codes.h"
#include "fnjson.h"
#include "fnsgml.h"

/**
 * Number of devices to expose via SIO, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

#define NEWDATA_SIZE 65535

class sioNetwork : public virtualDevice
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
#ifdef ESP_PLATFORM
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

    /**
     * Toggled by the rate limiting timer to indicate that the PROCEED interrupt should
     * be pulsed.
     */
    bool interruptProceed = false;

    /**
     * Called for SIO Command 'O' to open a connection to a network protocol, allocate all buffers,
     * and start the receive PROCEED interrupt.
     */
    virtual void sio_open(const FujiSIOPacket &packet);

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
    virtual void sio_read(const FujiSIOPacket &packet);

    /**
     * SIO Write command
     * Write # of bytes specified by aux1/aux2 from tx_buffer out to SIO. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    virtual void sio_write(const FujiSIOPacket &packet);

    /**
     * SIO Special, called as a default for any other SIO command not processed by the other sio_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either sio_complete()
     * or sio_error() is called.
     */
    void sio_status(const FujiSIOPacket &packet) override;

    /**
     * @brief set channel mode, JSON or PROTOCOL
     */
    virtual void sio_set_channel_mode(const FujiSIOPacket &packet);

    /**
     * @brief Called to set prefix
     */
    virtual void sio_set_prefix();

    /**
     * @brief Called to get prefix
     */
    virtual void sio_get_prefix();

    /**
     * @brief called to set login
     */
    virtual void sio_set_login();

    /**
     * @brief called to set password
     */
    virtual void sio_set_password();

    /**
     * @brief Get DSTATS value for a given network command
     * Allows programs to query the data direction for any command.
     */
    virtual void sio_get_dstats_value(const FujiSIOPacket &packet);

    /**
     * @brief called to seek to a file position (POINT)
     */
    virtual void sio_seek();

    /**
     * @brief called to return the current file position (NOTE)
     */
    virtual void sio_tell();

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    void sio_poll_interrupt();

    /**
     * Process incoming SIO command for device 0x7X
     * @param comanddata incoming 4 bytes containing command and aux bytes
     * @param checksum 8 bit checksum
     */
    void sio_process(const FujiSIOPacket &packet) override;
    void process_fs(const FujiSIOPacket &packet);
    void process_tcp(const FujiSIOPacket &packet);
    void process_http(const FujiSIOPacket &packet);
    void process_udp(const FujiSIOPacket &packet);

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
    std::unique_ptr<NetworkProtocol> protocol = nullptr;

    /**
     * Network Status object
     */
    NetworkStatus status;

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
#ifdef ESP_PLATFORM
    esp_timer_handle_t rateTimerHandle = nullptr;
#else
    uint64_t lastInterruptMs;
#endif

    /**
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by SIO call 0x2C
     */
    std::string prefix;

    /**
     * The Translation mode ORed into AUX2 for READ/WRITE/STATUS operations.
     * 0 = No Translation, 1 = CR<->EOL (Macintosh), 2 = LF<->EOL (UNIX), 3 = CR/LF<->EOL (PC/Windows)
     */
    netProtoTranslation_t trans_aux2 = NETPROTO_TRANS_NONE;

    /**
     * Client-set override for the computer's native EOL. Empty means use the
     * platform default assigned in instantiate_protocol(). See sio_set_eol().
     */
    std::string native_eol_override;

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
     * Timer Rate for interrupt timer (ms)
     */
#ifdef ESP_PLATFORM
    int timerRate = 100;
#else
    int timerRate = 20;
#endif

    /**
     * The channel mode for the currently open SIO device. By default, it is PROTOCOL, which passes
     * read/write/status commands to the protocol. Otherwise, it's a special mode, e.g. to pass to
     * the JSON or XML parsers.
     *
     * @enum PROTOCOL Send to protocol
     * @enum JSON Send to JSON parser.
     * @enum SGML Send to SGML/HTML/XML parser.
     */
    enum _channel_mode
    {
        PROTOCOL,
        JSON,
        SGML
    } channelMode;

    /**
     * saved NetworkStatus items
     */
    unsigned char reservedSave = 0;
    nDevStatus_t errorSave = NDEV_STATUS::SUCCESS;

    /**
     * The fnJSON parser wrapper object
     */
    FNJSON *json = nullptr;

    /**
     * Bytes sent of current JSON query object.
     */
    // FIXME - don't cache this, ask the fnJSON parser!
    unsigned short json_bytes_remaining = 0;

    /**
     * The fnSGML parser wrapper object (HTML/XML via CSS selector)
     */
    FNSGML *sgml = nullptr;

    /**
     * Bytes remaining of current SGML query result.
     */
    unsigned short sgml_bytes_remaining = 0;

    /**
     * @brief the write buffer
     */
    std::vector<uint8_t> newData;

    /**
     * Instantiate protocol object
     * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
     */
    success_is_true instantiate_protocol();

    /**
     * Create the deviceSpec and fix it for parsing
     */
    void create_devicespec(bool is_dir);

    /**
     * Create a urlParser from deviceSpec
    */
   void create_url_parser();

    /**
     * Get the DSTATS value for a given network command
     * @param command The network command code
     * @return The DSTATS byte value (0x00, 0x40, 0x80, or 0xFF for invalid)
     */
    uint8_t get_dstats_for_command(fujiCommandID_t command);

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
    void processCommaFromDevicespec(fujiDeviceID_t device);

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Passed directly to bus_to_computer().
     */
    fujiError_t sio_read_channel(unsigned short num_bytes);

    /**
     * @brief Perform read of the current JSON channel
     * @param num_bytes Number of bytes to read
     */
    fujiError_t sio_read_channel_json(unsigned short num_bytes);

    /**
     * @brief Perform read of the current SGML channel
     * @param num_bytes Number of bytes to read
     */
    fujiError_t sio_read_channel_sgml(unsigned short num_bytes);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Used to emit sio_error or sio_complete().
     */
    fujiError_t sio_write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void sio_status_local(const FujiSIOPacket &packet);

    /**
     * @brief perform channel status commands, if there is a protocol bound.
     */
    void sio_status_channel();

    /**
     * @brief get JSON status (# of bytes in receive channel)
     */
    error_is_true sio_status_channel_json(NetworkStatus *ns);

    /**
     * @brief get SGML status (# of bytes in receive channel)
     */
    error_is_true sio_status_channel_sgml(NetworkStatus *ns);

    /**
     * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void sio_assert_interrupt();

#ifndef ESP_PLATFORM
    /**
     * Called to clear the PROCEED interrupt
     */
    void sio_clear_interrupt();
#endif

    /**
     * @brief set translation specified by aux1 to aux2_translation mode.
     */
    void sio_set_translation(const FujiSIOPacket &packet);

    /**
     * @brief set the computer's native EOL from aux1/aux2 (aux1==0 restores default).
     */
    void sio_set_eol(const FujiSIOPacket &packet);

    /**
     * @brief Parse incoming JSON. (must be in JSON channelMode)
     */
    void sio_parse_json();

    /**
     * @brief Set JSON query string. (must be in JSON channelMode)
     */
    void sio_set_json_query(const FujiSIOPacket &packet);

    /**
     * @brief Set JSON parameters. (must be in JSON channelMode)
     * Used to affect values on the JSON object
     */
    void sio_set_json_parameters(const FujiSIOPacket &packet);

    /**
     * @brief Parse incoming SGML/HTML/XML. (must be in SGML channelMode)
     */
    void sio_parse_sgml();

    /**
     * @brief Set SGML CSS selector query string. (must be in SGML channelMode)
     */
    void sio_set_sgml_query(const FujiSIOPacket &packet);

    /**
     * @brief Set timer rate for PROCEED timer in ms
     */
    void sio_set_timer_rate(const FujiSIOPacket &packet);

    /**
     * @brief parse URL and instantiate protocol
     */
    void parse_and_instantiate_protocol(bool is_dir);
};

#endif /* NETWORK_H */
