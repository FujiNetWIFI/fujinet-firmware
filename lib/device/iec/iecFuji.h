#ifdef BUILD_IEC
#ifndef IECFUJI_H
#define IECFUJI_H

#include "fujiDevice.h"

// #include <cstdint>
// #include <cstring>

// #include "bus.h"
// #include "network.h"
// #include "cassette.h"
#include "fnWiFi.h"

// #include "../fuji/fujiHost.h"
// #include "../fuji/fujiDisk.h"
// #include "../fuji/fujiCmd.h"

// #include "hash.h"

// #define MAX_HOSTS 8
// #define MAX_DISK_DEVICES 8
// #define MAX_NETWORK_DEVICES 4

// // only in BASIC:
// #define MAX_APPKEY_LEN 64

// typedef struct
// {
//     char ssid[33];
//     char password[64];
//     char hostname[64];
//     unsigned char localIP[4];
//     unsigned char gateway[4];
//     unsigned char netmask[4];
//     unsigned char dnsIP[4];
//     unsigned char macAddress[6];
//     unsigned char bssid[6];
//     char fn_version[15];
// } AdapterConfig;

// typedef struct
// {
//     char ssid[33];
//     char hostname[64];
//     unsigned char localIP[4];
//     unsigned char gateway[4];
//     unsigned char netmask[4];
//     unsigned char dnsIP[4];
//     unsigned char macAddress[6];
//     unsigned char bssid[6];
//     char fn_version[15];
//     char sLocalIP[16];
//     char sGateway[16];
//     char sNetmask[16];
//     char sDnsIP[16];
//     char sMacAddress[18];
//     char sBssid[18];
// } AdapterConfigExtended;

// enum appkey_mode : int8_t
// {
//     APPKEYMODE_INVALID = -1,
//     APPKEYMODE_READ = 0,
//     APPKEYMODE_WRITE,
//     APPKEYMODE_READ_256
// };

// struct appkey
// {
//     uint16_t creator = 0;
//     uint8_t app = 0;
//     uint8_t key = 0;
//     appkey_mode mode = APPKEYMODE_INVALID;
//     uint8_t reserved = 0;
// } __attribute__((packed));

// typedef struct
// {
//     char ssid[33];
//     uint8_t rssi;
// } scan_result_t;

// typedef struct
// {
//     char ssid[MAX_SSID_LEN + 1];
//     char password[MAX_PASSPHRASE_LEN + 1];
// } net_config_t;

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
    void transaction_complete() override {}
    void transaction_error() override {}
    bool transaction_get(void *data, size_t len) override {return false;}
    void transaction_put(void *data, size_t len, bool err) override {}

#ifdef UNUSED
    systemBus *_bus;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    iecDrive _bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;
#endif /* UNUSED */

    AdapterConfig cfg;

#ifdef UNUSED
    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;
#endif /* UNUSED */

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
#ifdef NOT_SUBCLASS
    std::string process_directory_entry(uint8_t maxlen, uint8_t addtlopts);
#endif /* NOT_SUBCLASS */

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

#ifdef UNUSED
    // 0xFF
    void reset_device();
#endif /* UNUSED */
    
    // 0xFE
#ifdef NOT_SUBCLASS
    net_config_t net_get_ssid();
#endif /* NOT_SUBCLASS */
    void net_get_ssid_basic();
    void net_get_ssid_raw();

    // 0xFD
#ifdef NOT_SUBCLASS
    void net_scan_networks();
#endif /* NOT_SUBCLASS */
    void net_scan_networks_basic();
    void net_scan_networks_raw();

    // 0xFC
#ifdef NOT_SUBCLASS
    scan_result_t net_scan_result(int scan_num);
#endif /* NOT_SUBCLASS */
    void net_scan_result_basic();
    void net_scan_result_raw();

    // 0xFB
#ifdef NOT_SUBCLASS
    void net_set_ssid(bool store, net_config_t& net_config);
#endif /* NOT_SUBCLASS */
    void net_set_ssid_basic(bool store = true);
    void net_set_ssid_raw(bool store = true);

    // 0xFA
#ifdef NOT_SUBCLASS
    uint8_t net_get_wifi_status();
#endif /* NOT_SUBCLASS */
    void net_get_wifi_status_basic();
    void net_get_wifi_status_raw();

    // 0xF9
#ifdef NOT_SUBCLASS
    bool mount_host(int hs);
#endif /* NOT_SUBCLASS */
    void mount_host_basic();
    void mount_host_raw();

    // 0xF8
#ifdef NOT_SUBCLASS
    bool disk_image_mount(uint8_t ds, uint8_t mode);
#endif /* NOT_SUBCLASS */
    void disk_image_mount_basic();
    void disk_image_mount_raw();

    // 0xF7
#ifdef NOT_SUBCLASS
    bool open_directory(uint8_t hs, std::string dirpath, std::string pattern);
#endif /* NOT_SUBCLASS */
    void open_directory_basic();
    void open_directory_raw();

    // 0xF6
#ifdef NOT_SUBCLASS
    std::string read_directory_entry(uint8_t maxlen, uint8_t addtlopts);
#endif /* NOT_SUBCLASS */
    void read_directory_entry_basic();
    void read_directory_entry_raw();

    // 0xF5
#ifdef NOT_SUBCLASS
    void close_directory();
#endif /* NOT_SUBCLASS */
    void close_directory_basic();
    void close_directory_raw();

    // 0xF4
#ifdef NOT_SUBCLASS
    void read_host_slots(); // all handled in the basic/raw versions as they differ in functionality
#endif /* NOT_SUBCLASS */
    void read_host_slots_basic();
    void read_host_slots_raw();

    // 0xF3
#ifdef NOT_SUBCLASS
    void write_host_slots(); // all handled in the basic/raw versions as they differ in functionality
#endif /* NOT_SUBCLASS */
    void write_host_slots_basic();
    void write_host_slots_raw();

    // 0xF2
#ifdef NOT_SUBCLASS
    void read_device_slots();  // all handled in the basic/raw versions as they differ in functionality
#endif /* NOT_SUBCLASS */
    void read_device_slots_basic();
    void read_device_slots_raw();

    // 0xF1
#ifdef NOT_SUBCLASS
    void write_device_slots();
#endif /* NOT_SUBCLASS */
    void write_device_slots_basic();
    void write_device_slots_raw();

    // 0xF0
#ifdef NOT_SUBCLASS
    void enable_udpstream();
#endif /* NOT_SUBCLASS */

    // 0xEA
#ifdef NOT_SUBCLASS
    uint8_t net_get_wifi_enabled();
#endif /* NOT_SUBCLASS */
    void net_get_wifi_enabled_raw();

    // 0xE9
#ifdef NOT_SUBCLASS
    bool disk_image_umount(uint8_t deviceSlot);
#endif /* NOT_SUBCLASS */
    void disk_image_umount_basic();
    void disk_image_umount_raw();

    // 0xE8
#ifdef NOT_SUBCLASS
    void get_adapter_config();
#endif /* NOT_SUBCLASS */
    void get_adapter_config_basic();
    void get_adapter_config_raw();

    // 0xC4
#ifdef NOT_SUBCLASS
    AdapterConfigExtended get_adapter_config_extended();
#endif /* NOT_SUBCLASS */
    void get_adapter_config_extended_raw();

    // 0xE7
    void new_disk();

    // 0xE6
#ifdef NOT_SUBCLASS
    bool unmount_host(uint8_t hs);
#endif /* NOT_SUBCLASS */
    void unmount_host_basic();
    void unmount_host_raw();

    // 0xE5
#ifdef NOT_SUBCLASS
    uint16_t get_directory_position();
#endif /* NOT_SUBCLASS */
    void get_directory_position_basic();
    void get_directory_position_raw();

    // 0xE4
#ifdef NOT_SUBCLASS
    bool set_directory_position(uint16_t pos);
#endif /* NOT_SUBCLASS */
    void set_directory_position_basic();
    void set_directory_position_raw();

    // 0xE3
    void set_hindex();

    // 0xE2
#ifdef NOT_SUBCLASS
    void set_device_filename(uint8_t slot, uint8_t host, uint8_t mode, std::string filename);
#endif /* NOT_SUBCLASS */
    void set_device_filename_basic();
    void set_device_filename_raw();

    // 0xE1
#ifdef NOT_SUBCLASS
    void set_host_prefix();
#endif /* NOT_SUBCLASS */

    // 0xE0
#ifdef NOT_SUBCLASS
    void get_host_prefix();
#endif /* NOT_SUBCLASS */

    // 0xDF
#ifdef NOT_SUBCLASS
    void set_external_clock();
#endif /* NOT_SUBCLASS */

    // 0xDE
#ifdef NOT_SUBCLASS
    int write_app_key(std::vector<uint8_t>&& value);
#endif /* NOT_SUBCLASS */
    void write_app_key_basic();
    void write_app_key_raw();

    // 0xDD
#ifdef NOT_SUBCLASS
    int read_app_key(char *filename, std::vector<uint8_t>& file_data);
#endif /* NOT_SUBCLASS */
    void read_app_key_basic();
    void read_app_key_raw();

    // 0xDC
#ifdef NOT_SUBCLASS
    void open_app_key(uint16_t creator, uint8_t app, uint8_t key, appkey_mode mode, uint8_t reserved);
#endif /* NOT_SUBCLASS */
    void open_app_key_basic();
    void open_app_key_raw();

    // 0xDB
#ifdef NOT_SUBCLASS
    void close_app_key();
#endif /* NOT_SUBCLASS */
    void close_app_key_basic();
    void close_app_key_raw();

    // 0xDA
#ifdef NOT_SUBCLASS
    std::string get_device_filename(uint8_t ds);
#endif /* NOT_SUBCLASS */
    void get_device_filename_basic();
    void get_device_filename_raw();

    // 0xD9
#ifdef NOT_SUBCLASS
    void set_boot_config(bool should_boot_config);
#endif /* NOT_SUBCLASS */
    void set_boot_config_basic();
    void set_boot_config_raw();

    // 0xD8
#ifdef NOT_SUBCLASS
    void copy_file();
#endif /* NOT_SUBCLASS */

    // 0xD6
#ifdef NOT_SUBCLASS
    void set_boot_mode(uint8_t boot_device, bool should_boot_config);
#endif /* NOT_SUBCLASS */
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
    void enable_device_basic();
    void disable_device_basic();

#ifdef UNUSED
    int appkey_size = 64;
    std::map<int, int> mode_to_keysize = {
        {0, 64},
        {2, 256}
    };
#endif /* UNUSED */
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
#ifdef UNUSED
    bool boot_config = true;

    bool status_wait_enabled = true;

    //iecNetwork *network();

    iecDrive *bootdisk();

    void insert_boot_device(uint8_t d);
#endif /* UNUSED */

    void setup(systemBus *bus);

#ifdef UNUSED
    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    // 0xD7 - why is this public?
    void mount_all();

    // overriding the IECDevice isActive() function because device_active
    // must be a global variable
    bool device_active = true;
    virtual bool isActive() { return device_active; }
#endif /* UNUSED */

    iecFuji();
};

#endif // IECFUJI_H
#endif /* BUILD_IEC */
