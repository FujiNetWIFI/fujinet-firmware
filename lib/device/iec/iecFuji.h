#ifdef BUILD_IEC
#ifndef IECFUJI_H
#define IECFUJI_H

#include "fujiDevice.h"
#include "FujiIECPacket.h"
#include "fnWiFi.h"

class iecFuji : public fujiDevice
{
protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    AdapterConfig cfg;

    bool is_raw_command;

    bool processCommand(const FUJI_COMMAND_PACKET &packet) override;

    std::vector<std::string> tokenize_basic_command(std::string command);

    success_is_true validate_directory_slot();

    // is the cmd supported by RAW?
    bool is_supported(const FujiIECPacket &packet);

    // Commodore specific
    void update_firmware();

    void set_fuji_iec_status(int8_t error, const std::string msg) {
        set_iec_status(error, (uint8_t) _activePacket->command(), msg, fnWiFi.connected(), 15);
    }

    void set_iec_status(int8_t error, uint8_t cmd, const std::string msg, bool connected, int channel) {
        iecStatus.error = error;
        iecStatus.cmd = cmd;
        iecStatus.msg = msg;
        iecStatus.connected = connected;
        iecStatus.channel = channel;
    }

    // TODO: does this need to translate the message to PETSCII?
    std::vector<uint8_t> iec_status_to_vector() {
        std::vector<uint8_t> data;
        data.push_back(static_cast<uint8_t>(iecStatus.error));
        data.push_back(iecStatus.cmd);
        data.push_back(iecStatus.connected ? 1 : 0);
        data.push_back(static_cast<uint8_t>(iecStatus.channel & 0xFF)); // it's only an int because of atoi from some basic commands, but it's never really more than 1 byte

        // max of 41 chars in message including the null terminator. It will simply be truncated, so if we find any that are excessive, should trim them down in firmware
        size_t actualLength = std::min(iecStatus.msg.length(), static_cast<size_t>(40));
        for (size_t i = 0; i < actualLength; ++i) {
            data.push_back(static_cast<uint8_t>(iecStatus.msg[i]));
        }
        data.push_back(0); // null terminate the string

        return data;
    }

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

public:
    void setup();
    iecFuji();
};

#endif // IECFUJI_H
#endif /* BUILD_IEC */
