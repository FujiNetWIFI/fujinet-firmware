#ifndef NETWORK_H
#define NETWORK_H

#include <esp_timer.h>

#include <string>

#include "../../bus/bus.h"

#include "../EdUrlParser/EdUrlParser.h"

#include "../network-protocol/Protocol.h"

#include "../fnjson/fnjson.h"

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
    FNJSON json;

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
     * @brief Deal with commands sent to command channel
     */
    void process_command();

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
};

#endif /* NETWORK_H */