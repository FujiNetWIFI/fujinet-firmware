#ifndef NETWORK_H
#define NETWORK_H

#include <esp_timer.h>

#include <string>

#include "../../bus/bus.h"

#include "../EdUrlParser/EdUrlParser.h"

#include "../network-protocol/Protocol.h"

#include "../fnjson/fnjson.h"

#include "string_utils.h"

/**
 * # of devices to expose via IEC
 */
#define NUM_DEVICES 2

/**
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

class iecNetwork : public virtualDevice
{
public:
    /**
     * Command frame for protocol adapters
     */
    cmdFrame_t cmdFrame;

    /**
     * CTOR
     */
    iecNetwork();

    /**
     * DTOR
     */
    virtual ~iecNetwork();

    // Status
    void status()
    {
        // TODO IMPLEMENT
    }

protected:
    device_state_t process(IECData *commanddata) override;
    void shutdown() override;

private:
    /**
     * JSON Object
     */
    FNJSON *json[16];

    /**
     * The Receive buffers for this N: device
     */
    std::string *receiveBuffer[16] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};

    /**
     * The transmit buffers for this N: device
     */
    std::string *transmitBuffer[16] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};

    /**
     * The special buffers for this N: device
     */
    std::string *specialBuffer[16] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};

    /**
     * The EdUrlParser object used to hold/process a URL
     */
    EdUrlParser *urlParser[16] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};

    /**
     * Instance of currently open network protocol
     */
    NetworkProtocol *protocol[16] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};

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
     * @brief the Device spec currently open (N:TCP://192.168.1.1:1234/)
     */
    string deviceSpec[16];

    /**
     * The channel mode for a given IEC subdevice. By default, it is PROTOCOL, which passes
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
    } channelMode[15] =
        {
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL,
            PROTOCOL
        };

    /**
     * @brief the current translation mode for given channel.
     */
    uint8_t translationMode[16] = 
    {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    /**
     * The login to use for a protocol action
     */
    std::string login;

    /**
     * The password to use for a protocol action
     */
    std::string password;

    /**
     * @brief The currently set path prefix.
     */
    std::string prefix[16];

    /**
     * @brief # of bytes remaining in JSON stream
     */
    uint16_t json_bytes_remaining[16] = 
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    /**
     * @brief respond to OPEN command ($F0)
     */
    void iec_open();

    /**
     * @brief response to CLOSE command ($E0)
     */
    void iec_close();

    /**
     * @brief response to DATA command on LOAD channel ($60)
     */
    void iec_reopen_load();

    /**
     * @brief response to DATA command on SAVE channel ($60)
     */
    void iec_reopen_save();

    /**
     * @brief response to DATA command on any other channel ($60)
     */
    void iec_reopen_channel();

    /**
     * @brief Computer->FujiNet write
     */
    void iec_write();

    /**
     * @brief FujiNet->Com puter read
     */
    void iec_read();

    /**
     * @brief return data waiting.
     */
    void data_waiting();

    /**
     * @brief handle 
    */

    /**
     * @brief set network prefix on desired device.
     */
    void set_prefix();

    /**
     * @brief Parse JSON in currently open channel
     */
    void parse_json();

    /**
     * @brief set desired translation mode
     */
    void set_translation();

    /**
     * @brief Set channel mode
     */
    void set_channel_mode();

    /**
     * @brief Set Login/password
     */
    void set_login();

    /**
     * @brief perform idempotent filesystem op via protocol
     * @param _comnd the command to send to protocol
     */
    void fsop(unsigned char _comnd);

    /**
     * @brief perform JSON Query 
     */
    void set_json_query();

    /**
     * @brief Deal with commands sent to command channel
     */
    void process_command();

    /**
     * @brief Called to process a special command
     */
    void process_command_special();

    /**
     * @brief Call protocol to process special command
     */
    void process_command_special_protocol();

    /**
     * @brief Deal with URLs passed to load channel
     */
    void process_load();

    /**
     * @brief Deal with URLs passed to save channel
     */
    void process_save();

    /**
     * @brief Deal with URLs passed to data channels (3-14)
     */
    void process_channel();

    /**
     * @brief read from desired channel
     * @param l Number of bytes to read
     */
    void read_channel(uint16_t l);

    /**
     * @brief read channel, protocol mode JSON
     * @param l Number of bytes to read
     */
    void read_channel_json(uint16_t l);
};

#endif /* NETWORK_H */