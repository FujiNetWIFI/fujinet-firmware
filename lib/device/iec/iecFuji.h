#ifdef BUILD_IEC
#ifndef IECFUJI_H
#define IECFUJI_H

#include "fujiDevice.h"
#include "fnWiFi.h"

// Isn't this something global to all IEC devices and should be part of the bus?
typedef enum
{
    DEVICE_ERROR = -1,
    DEVICE_IDLE = 0,      // Ready and waiting
    DEVICE_ACTIVE = 1,
    DEVICE_LISTEN = 2,    // A command is recieved and data is coming to us
    DEVICE_TALK = 3,      // A command is recieved and we must talk now
    DEVICE_PAUSED = 4,    // Execute device command
} device_state_t;


class iecFuji : public fujiDevice
{
protected:
    void transaction_continue(bool expectMoreData) override {}
    void transaction_complete() override {}
    void transaction_error() override {}
    bool transaction_get(void *data, size_t len) override {return false;}
    void transaction_put(const void *data, size_t len, bool err) override {
        response.clear();
        response.append(reinterpret_cast<const char*>(data), len);
    }

    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    AdapterConfig cfg;

    std::vector<std::string> pt;
    std::string payloadRaw, payload, response;
    std::vector<uint8_t> responseV;
    size_t responsePtr;
    bool is_raw_command;

    void process_cmd();
    void process_raw_cmd_data();
    void process_immediate_raw_cmds();

    void process_basic_commands();
    std::vector<std::string> tokenize_basic_command(std::string command);

    bool validate_parameters_and_setup(uint8_t& maxlen, uint8_t& addtlopts);
    bool validate_directory_slot();

    // track what our current command is, -1 is none being processed.
    int current_fuji_cmd = -1;
    // track the last command for the status
    int last_command = -1;

    virtual void talk(uint8_t secondary) override;
    virtual void listen(uint8_t secondary) override;
    virtual void untalk() override;
    virtual void unlisten() override;
    virtual int8_t canWrite() override;
    virtual int8_t canRead() override;
    virtual void write(uint8_t data, bool eoi) override;
    virtual uint8_t read() override;
    virtual void task() override;
    virtual void reset() override;

    // is the cmd supported by RAW?
    bool is_supported(uint8_t cmd);

    // 0xFE
    void net_get_ssid_basic();
    void net_get_ssid_raw();

    // 0xFD
    void net_scan_networks_basic();
    void net_scan_networks_raw();

    // 0xFC
    void net_scan_result_basic();
    void net_scan_result_raw();

    // 0xFB
    void net_set_ssid_basic(bool store = true);
    void net_set_ssid_raw(bool store = true);

    // 0xFA
    void net_get_wifi_status_basic();
    void net_get_wifi_status_raw();

    // 0xF9
    void mount_host_basic();
    void mount_host_raw();

    // 0xF8
    void mount_disk_image_basic();
    void mount_disk_image_raw();

    // 0xF7
    void open_directory_basic();
    void open_directory_raw();

    // 0xF6
    void read_directory_entry_basic();
    void read_directory_entry_raw();

    // 0xF5
    void close_directory_basic();
    void close_directory_raw();

    // 0xF4
    void read_host_slots_basic();
    void read_host_slots_raw();

    // 0xF3
    void write_host_slots_basic();
    void write_host_slots_raw();

    // 0xF2
    void read_device_slots_basic();
    void read_device_slots_raw();

    // 0xF1
    void write_device_slots_basic();
    void write_device_slots_raw();

    // 0xEA
    void net_get_wifi_enabled_raw();

    // 0xE9
    void unmount_disk_image_basic();
    void unmount_disk_image_raw();

    // 0xE8
    void get_adapter_config_basic();
    void get_adapter_config_raw();

    // 0xC4
    void get_adapter_config_extended_raw();

    // 0xE7
    void new_disk();

    // 0xE6
    void unmount_host_basic();
    void unmount_host_raw();

    // 0xE5
    void get_directory_position_basic();
    void get_directory_position_raw();

    // 0xE4
    void set_directory_position_basic();
    void set_directory_position_raw();

    // 0xE3
    void set_hindex();

    // 0xE2
    void set_device_filename_basic();
    void set_device_filename_raw();

    // 0xDE
    void write_app_key_basic();
    void write_app_key_raw();

    // 0xDD
    void read_app_key_basic();
    void read_app_key_raw();

    // 0xDC
    void open_app_key_basic();
    void open_app_key_raw();

    // 0xDB
    void close_app_key_basic();
    void close_app_key_raw();

    // 0xDA
    void get_device_filename_basic();
    void get_device_filename_raw();

    // 0xD9
    void set_boot_config_basic();
    void set_boot_config_raw();

    // 0xD6
    void set_boot_mode_basic();
    void set_boot_mode_raw();

    // 0x53 'S' Status
    void get_status_raw();
    void get_status_basic();

    // 0xC8
    void hash_input(std::string input);
    void hash_input_raw();

    // 0xC7, 0xC3
    void hash_compute(bool clear_data, Hash::Algorithm alg);
    void hash_compute_raw(bool clear_data);

    // 0xC6
    uint8_t hash_length(bool is_hex);
    void hash_length_raw();

    // 0xC5
    std::vector<uint8_t> hash_output(bool is_hex);
    void hash_output_raw();

    // 0xC2
    void hash_clear();
    void hash_clear_raw();

    // Commodore specific
    void local_ip();
    void netmask();
    void gateway();
    void dns_ip();
    void mac_address();
    void bssid();
    void fn_version();

    void enable_device_basic();
    void disable_device_basic();

    bool check_appkey_creator(bool check_is_write);

    void set_fuji_iec_status(int8_t error, const std::string msg) {
        set_iec_status(error, last_command, msg, fnWiFi.connected(), 15);
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

public:
    void setup();
    iecFuji();
};

#endif // IECFUJI_H
#endif /* BUILD_IEC */
