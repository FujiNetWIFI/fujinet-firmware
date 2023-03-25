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
 * The size of rx and tx buffers
 */
#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535
#define SPECIAL_BUFFER_SIZE 256

/**
 * The number of IEC secondary addresses (16)
 */
#define NUM_CHANNELS 16

using namespace std;

class iecNetwork : public virtualDevice
{    
    public:

    /**
     * @brief CTOR
     */
    iecNetwork();

    /**
     * @brief the Receive buffers, for each channel
     */
    string *receiveBuffer[NUM_CHANNELS];
    
    /**
     * @brief the Transmit buffers, one for each channel.
     */
    string *transmitBuffer[NUM_CHANNELS];

    /**
     * @brief the Special buffers, one for each channel.
     */
    string *specialBuffer[NUM_CHANNELS];

    /**
     * @brief the protocol instance for given channel
    */
    NetworkProtocol *protocol[NUM_CHANNELS];

    /**
     * @brief DTOR
     */
    virtual ~iecNetwork();

    /**
     * @brief Process command fanned out from bus
     * @param _commanddata the passed in commanddata
     * @return new device state
     */
    device_state_t process(IECData *_commanddata);

    private:

    /**
     * @brief the active URL for each channel
     */
    string deviceSpec[NUM_CHANNELS];

    /**
     * @brief the URL parser for each channel
     */
    EdUrlParser *urlParser[NUM_CHANNELS];

    /**
     * @brief the prefix for each channel
     */
    string prefix[NUM_CHANNELS];

    /**
     * @brief the active Channel mode for each channel
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
     * @brief the active translation mode for each channel 
     */
    uint8_t translationMode[NUM_CHANNELS];

    /**
     * @brief the login (username) for each channel
     */
    string login[NUM_CHANNELS];

    /**
     * @brief the password for each channel
     */
    string password[NUM_CHANNELS];

    /**
     * @brief the JSON object for each channel
     */
    FNJSON *json[NUM_CHANNELS];

    /**
     * @brief signal file not found
     */
    bool file_not_found = false;

    /**
     * @brief The status information to send back on cmd input
     * @param bw = # of bytes waiting
     * @param msg = most recent status message
     * @param connected = is most recent channel connected?
     * @param channel = channel of most recent status msg.
     */
    struct _iecStatus
    {
        uint16_t bw;
        string msg;
        bool connected;
        int channel;
    } iecStatus;

    /**
     * @brief Set desired prefix for channel
     */
    void set_prefix();

    /**
     * @brief Get prefix for channel
     */
    void get_prefix();

    /**
     * @brief Set login/password
     */
    void set_login_password();

    /**
     * @brief called to open a connection to a protocol
     */
    void iec_open();

    /**
     * @brief called to close a connection.
     */
    void iec_close();
    
    /**
     * @brief called when a TALK, then REOPEN happens on channel 0
     */
    void iec_reopen_load();

    /**
     * @brief called when TALK, then REOPEN happens on channel 1
     */
    void iec_reopen_save();

    /**
     * @brief called when LISTEN happens on command channel (15).
     */
    void iec_listen_command();

    /**
     * @brief called when TALK happens on command channel (15).
     */
    void iec_talk_command();

    /**
     * @brief called to process command either at open or listen
     */
    void iec_command();

    /**
     * @brief If response queue is empty, Return 1 if ANY receive buffer has data in it, else 0
     */
    void iec_talk_command_buffer_status();

    /**
     * @brief process command for channel 0 (load)
     */
    void process_load();

    /**
     * @brief process command for channel 1 (save)
     */
    void process_save();

    /**
     * @brief process command channel
     */
    void process_command();

    /**
     * @brief process every other channel (2-14)
     */
    void process_channel();

};

#endif /* NETWORK_H */