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
 * Number of devices to expose via DRIVEWIRE, becomes 0x71 to 0x70 + NUM_DEVICES - 1
 */
#define NUM_DEVICES 8

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class drivewireNetwork : public virtualDevice
{

public:
    /**
     * Constructor
     */
    drivewireNetwork();

    /**
     * Destructor
     */
    virtual ~drivewireNetwork();

    /**
     * @brief process network device command
     */
    bool processCommand(const FujiDWPacket &packet) override;
    void process_fs(const FujiDWPacket &packet);
    void process_tcp(const FujiDWPacket &packet);
    void process_http(const FujiDWPacket &packet);
    void process_udp(const FujiDWPacket &packet);

    /**
     * Check to see if PROCEED needs to be asserted.
     */
    bool poll_interrupt();

    /**
     * Called for DRIVEWIRE Command 'O' to open a connection to a network protocol, allocate all buffers,
     */
    void open(fileAccessMode_t access, netProtoTranslation_t trans_mode);

    /**
     * Called for DRIVEWIRE Command 'C' to close a connection to a network protocol, de-allocate all buffers,
     */
    void close();

    /**
     * DRIVEWIRE Read command
     * Read # of bytes from the protocol adapter specified, into the RX buffer. If we are short
     * fill the rest with nulls and return ERROR.
     *
     * @note It is the channel's responsibility to pad to required length.
     */
    void read(uint16_t num_bytes);

    /**
     * DRIVEWIRE Write command
     * Write # of bytes specified from tx_buffer out to DRIVEWIRE. If protocol is unable to return requested
     * number of bytes, return ERROR.
     */
    void write(uint16_t num_bytes);

    /**
     * DRIVEWIRE Special, called as a default for any other DRIVEWIRE command not processed by the other drivewire_ functions.
     * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
     * process the special command. Otherwise, the command is handled locally. In either case, either drivewire_complete()
     * or drivewire_error() is called.
     */
    void status(uint8_t mode);

    /**
     * @brief set channel mode, JSON or PROTOCOL
     */
    void set_channel_mode(uint8_t mode);

    /**
     * @brief Called to set prefix
     */
    void set_prefix();

    /**
     * @brief Called to get prefix
     */
    void get_prefix();

    /**
     * @brief called to set login
     */
    void set_login();

    /**
     * @brief called to set password
     */
    void set_password();

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
     * Devicespec passed to us, e.g. N:HTTP://WWW.GOOGLE.COM:80/
     */
    std::string deviceSpec;

    /**
     * The currently set Prefix for this N: device, set by DRIVEWIRE call 0x2C
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
     * The channel mode for the currently open DRIVEWIRE device. By default, it is PROTOCOL, which passes
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

    uint32_t readAck = 0;

    /**
     * Instantiate protocol object
     * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
     */
    bool instantiate_protocol();

    /**
     * Create the deviceSpec and fix it for parsing
     */
    void create_devicespec(bool is_dir);

    /**
     * Create a urlParser from deviceSpec
    */
   void create_url_parser();

    /**
     * Is this a valid URL? (used to generate ERROR 165)
     */
    bool isValidURL(PeoplesUrlParser *url);

    /**
     * Perform the correct read based on value of channelMode
     * @param num_bytes Number of bytes to read.
     * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Passed directly to bus_to_computer().
     */
    fujiError_t read_channel(unsigned short num_bytes);

    /**
     * @brief Perform read of the current JSON channel
     * @param num_bytes Number of bytes to read
     */
    fujiError_t read_channel_json(unsigned short num_bytes);

    /**
     * Perform the correct write based on value of channelMode
     * @param num_bytes Number of bytes to write.
     * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Used to emit drivewire_error or drivewire_complete().
     */
    fujiError_t write_channel(unsigned short num_bytes);

    /**
     * @brief perform local status commands, if protocol is not bound, based on cmdFrame
     * value.
     */
    void status_local(uint8_t req);

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
    void parse_json();

    /**
     * @brief Set JSON query std::string. (must be in JSON channelMode)
     */
    void json_query();

    /**
     * @brief parse URL and instantiate protocol
     */
    void parse_and_instantiate_protocol(bool is_dir);
};

#endif /* NETWORK_H */
