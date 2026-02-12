#ifndef NETWORK_H
#define NETWORK_H

#include <array>
#include <cstdint>
#include <esp_timer.h>
#include <memory>
#include <string>

#include "bus.h"
#include "fnjson.h"
#include "network_data.h"
#include "peoples_url_parser.h"
#include "Protocol.h"
#include "string_utils.h"
#include "../../bus/iec/IECFileDevice.h"

using namespace std;

/**
 * @class IECData
 * @brief the IEC command data passed to devices
 */
class IECData
{
public:
    /**
     * @brief the primary command byte
     */
    uint8_t primary = 0;
    /**
     * @brief the secondary command byte
     */
    uint8_t secondary = 0;
    /**
     * @brief the primary device number
     */
    uint8_t device = 0;
    /**
     * @brief the secondary command channel
     */
    uint8_t channel = 0;
    /**
     * @brief the device command
     */
    std::string payload = "";
    /**
     * @brief the raw bytes received for the command
     */
    std::vector<uint8_t> payload_raw;
    /**
     * @brief clear and initialize IEC command data
     */
    void init(void)
    {
        primary = 0;
        device = 0;
        secondary = 0;
        channel = 0;
        payload.clear();
        payload_raw.clear();
    }

    int channelCommand();
    void debugPrint();
};

class iecNetwork : public IECFileDevice
{
public:

    /**
     * @brief CTOR
     */
    iecNetwork(uint8_t devnr);

    /**
     * @brief DTOR
     */
    virtual ~iecNetwork();

    std::unordered_map<uint8_t, NetworkData> network_data_map;

protected:
    virtual void task() override;
    virtual bool open(uint8_t channel, const char *name) override;
    virtual void close(uint8_t channel) override;
    virtual uint8_t write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi) override;
    virtual uint8_t read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi) override;
    virtual void execute(const char *command, uint8_t cmdLen) override;
    virtual uint8_t getStatusData(char *buffer, uint8_t bufferSize) override;
    virtual void reset() override;

private:
    /**
     * @brief flag to indicate if the status result should be binary or string
     */
    bool is_binary_status = false;

    /**
     * @brief signal file not found
     */
    bool file_not_found = false;

    /**
     * @brief active status channel
     */
    uint8_t active_status_channel=0;

    /**
     * @brief parse JSON
     */
    void parse_json();

    /**
     * @brief query JSON
     */
    void query_json();

    /**
     * @brief parse into bite size strings
     */
    void parse_bite();

    /**
     * @brief Set device ID from dos command
     */
    void set_device_id();

    /**
     * @brief Set channel to retrieve status from.
     */
    void set_status(bool is_binary);

    /**
     * @brief Set desired prefix for channel
     */
    void set_prefix();

    /**
     * @brief Get prefix for channel
     */
    void get_prefix();

    /**
     * @brief Set channel mode (e.g. protocol, or json)
     */
    void set_channel_mode();

    /**
     * @brief Set login/password
     */
    void set_login_password();

    /**
     * @brief Set translation mode
     */
    void set_translation_mode();

    /**
     * @brief ask protocol to perform idempotent filesystem operation
     * @param comnd the command to pass in cmdFrame
     */
    void fsop(fujiCommandID_t comnd);

    /**
     * @brief called to open a connection to a protocol
     */
    void iec_open();

    /**
     * @brief called to close a connection.
     */
    void iec_close();

    /**
     * @brief called to process command either at open or listen
     */
    void iec_command();

    /**
     * @brief called to ask protocol to perform an operation with no payload
     */
    void perform_special_00();

    /**
     * @brief called to ask protocol to perform an operation with payload to computer (status)
     */
    void perform_special_40();

    /**
     * @brief called to ask protocol to perform an operation with no payload
     */
    void perform_special_80();

    /**
     * @brief changes the open mode for the channel (e.g. to DELETE)
     */
    void set_open_params();

    void init();
    bool transmit(NetworkData &channel_data);
    bool receive(NetworkData &channel_data, uint16_t rxBytes);

    /**
     * @brief The status information to send back on cmd input
     * @param error = the latest error status
     * @param msg = most recent status message
     * @param connected = is most recent channel connected?
     * @param channel = channel of most recent status msg.
     */
    struct _iecStatus
    {
        int8_t error;
        uint8_t cmd;
        std::string msg;
        bool connected;
        int channel;
    } iecStatus;

    std::vector<std::string> pt;
    IECData commanddata;
    std::string payload;
    cmdFrame_t cmdFrame;
};

#endif /* NETWORK_H */
