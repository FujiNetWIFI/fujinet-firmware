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

    ByteBuffer _payload;
    bool is_raw_command;

    void process_cmd();
    bool processCommand(const FUJI_COMMAND_PACKET &packet) override;

    std::vector<std::string> tokenize_basic_command(std::string command);

    success_is_true validate_directory_slot();

    void talk(uint8_t secondary) override;
    void listen(uint8_t secondary) override;
    void untalk() override;
    void unlisten() override;
    int8_t canWrite() override;
    int8_t canRead() override;
    void write(uint8_t data, bool eoi) override;
    uint8_t read() override;
    void task() override;
    void reset() override;

    // is the cmd supported by RAW?
    bool is_supported(const FujiIECPacket &packet);

    void net_get_ssid_raw();
    void net_scan_networks_raw();
    void net_scan_result_raw(const FujiIECPacket &packet);
    void net_set_ssid_raw(bool store = true);
    void net_get_wifi_status_raw();
    void mount_host_raw(const FujiIECPacket &packet);
    void mount_disk_image_raw(const FujiIECPacket &packet);
    void open_directory_raw(const FujiIECPacket &packet);
    void read_directory_entry_raw(const FujiIECPacket &packet);
    void close_directory_raw();
    void read_host_slots_raw();
    void write_host_slots_raw();
    void read_device_slots_raw();
    void write_device_slots_raw();
    void net_get_wifi_enabled_raw();
    void unmount_disk_image_raw(const FujiIECPacket &packet);
    void get_adapter_config_raw();
    void get_adapter_config_extended_raw();
    void new_disk();
    void unmount_host_raw(const FujiIECPacket &packet);
    void get_directory_position_raw();
    void set_directory_position_raw(const FujiIECPacket &packet);
    void set_device_filename_raw(const FujiIECPacket &packet);
    void write_app_key_raw(const FujiIECPacket &packet);
    void read_app_key_raw();
    void open_app_key_raw();
    void close_app_key_raw();
    void get_device_filename_raw(const FujiIECPacket &packet);
    void set_boot_config_raw(const FujiIECPacket &packet);
    void copy_file(std::string source, std::string destination);
    void set_boot_mode_raw(const FujiIECPacket &packet);
    void get_status_raw();

    // Commodore specific
    void update_firmware();

    // Commodore specific
    void local_ip();
    void netmask();
    void gateway();
    void dns_ip();
    void mac_address();
    void bssid();
    void fn_version();

    success_is_true check_appkey_creator(bool check_is_write);

    void set_fuji_iec_status(int8_t error, const std::string msg) {
        set_iec_status(error, _activePacket->command(), msg, fnWiFi.connected(), 15);
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

    device_state_t state;
    std::unique_ptr<FujiIECPacket> _activePacket;

public:
    void setup();
    iecFuji();
};

#endif // IECFUJI_H
#endif /* BUILD_IEC */
