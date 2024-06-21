#ifndef FUJI_H
#define FUJI_H

#include <cstdint>
#include <cstring>

#include "bus.h"
#include "network.h"
#include "cassette.h"
#include "fnWiFi.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"
#include "../fuji/fujiCmd.h"

#include "hash.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 4

// only in BASIC:
#define MAX_APPKEY_LEN 64

typedef struct
{
    char ssid[33];
    char password[64];
    char hostname[64];
    unsigned char localIP[4];
    unsigned char gateway[4];
    unsigned char netmask[4];
    unsigned char dnsIP[4];
    unsigned char macAddress[6];
    unsigned char bssid[6];
    char fn_version[15];
} AdapterConfig;

typedef struct
{
    char ssid[33];
    char hostname[64];
    unsigned char localIP[4];
    unsigned char gateway[4];
    unsigned char netmask[4];
    unsigned char dnsIP[4];
    unsigned char macAddress[6];
    unsigned char bssid[6];
    char fn_version[15];
    char sLocalIP[16];
    char sGateway[16];
    char sNetmask[16];
    char sDnsIP[16];
    char sMacAddress[18];
    char sBssid[18];
} AdapterConfigExtended;

enum appkey_mode : int8_t
{
    APPKEYMODE_INVALID = -1,
    APPKEYMODE_READ = 0,
    APPKEYMODE_WRITE,
    APPKEYMODE_READ_256
};

struct appkey
{
    uint16_t creator = 0;
    uint8_t app = 0;
    uint8_t key = 0;
    appkey_mode mode = APPKEYMODE_INVALID;
    uint8_t reserved = 0;
} __attribute__((packed));

typedef struct
{
    char ssid[33];
    uint8_t rssi;
} scan_result_t;

typedef struct
{
    char ssid[MAX_SSID_LEN + 1];
    char password[MAX_PASSPHRASE_LEN + 1];
} net_config_t;

class iecFuji : public virtualDevice
{
private:
    systemBus *_bus;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    iecDrive _bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

    AdapterConfig cfg;

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;

    std::string response;
    std::vector<uint8_t> responseV;
    bool is_raw_command;

    void process_raw_cmd_data();
    void process_immediate_raw_cmds();

    void process_basic_commands();
    vector<string> tokenize_basic_command(string command);

    bool validate_parameters_and_setup(uint8_t& maxlen, uint8_t& addtlopts);
    bool validate_directory_slot();
    std::string process_directory_entry(uint8_t maxlen, uint8_t addtlopts);

    // track what our current command is, -1 is none being processed.
    int current_fuji_cmd = -1;
    // track the last command for the status
    int last_command = -1;

protected:
    // is the cmd supported by RAW?
    bool is_supported(uint8_t cmd);

    // helper functions
    void net_store_ssid(std::string ssid, std::string password);

    // 0xFF
    void reset_device();

    // 0xFE
    net_config_t net_get_ssid();
    void net_get_ssid_basic();
    void net_get_ssid_raw();

    // 0xFD
    void net_scan_networks();
    void net_scan_networks_basic();
    void net_scan_networks_raw();

    // 0xFC
    scan_result_t net_scan_result(int scan_num);
    void net_scan_result_basic();
    void net_scan_result_raw();
    
    // 0xFB
    void net_set_ssid(bool store, net_config_t& net_config);
    void net_set_ssid_basic(bool store = true);
    void net_set_ssid_raw(bool store = true);

    // 0xFA
    uint8_t net_get_wifi_status();
    void net_get_wifi_status_basic();
    void net_get_wifi_status_raw();
    
    // 0xF9
    bool mount_host(int hs);
    void mount_host_basic();
    void mount_host_raw();

    // 0xF8
    bool disk_image_mount(uint8_t ds, uint8_t mode);
    void disk_image_mount_basic();
    void disk_image_mount_raw();

    // 0xF7
    bool open_directory(uint8_t hs, std::string dirpath, std::string pattern);
    void open_directory_basic();
    void open_directory_raw();

    // 0xF6
    std::string read_directory_entry(uint8_t maxlen, uint8_t addtlopts);
    void read_directory_entry_basic();
    void read_directory_entry_raw();

    // 0xF5
    void close_directory();
    void close_directory_basic();
    void close_directory_raw();

    // 0xF4
    // void read_host_slots(); // all handled in the basic/raw versions as they differ in functionality
    void read_host_slots_basic();
    void read_host_slots_raw();

    // 0xF3
    // void write_host_slots(); // all handled in the basic/raw versions as they differ in functionality
    void write_host_slots_basic();
    void write_host_slots_raw();

    // 0xF2
    // void read_device_slots();  // all handled in the basic/raw versions as they differ in functionality
    void read_device_slots_basic();
    void read_device_slots_raw();

    // 0xF1
    void write_device_slots();
    void write_device_slots_basic();
    void write_device_slots_raw();
    
    // 0xF0
    void enable_udpstream();
    
    // 0xEA
    uint8_t net_get_wifi_enabled();
    void net_get_wifi_enabled_raw();
    
    // 0xE9
    bool disk_image_umount(uint8_t deviceSlot);
    void disk_image_umount_basic();
    void disk_image_umount_raw();
    
    // 0xE8
    void get_adapter_config();
    void get_adapter_config_basic();
    void get_adapter_config_raw();

    // 0xC4
    AdapterConfigExtended get_adapter_config_extended();
    void get_adapter_config_extended_raw();
    
    // 0xE7
    void new_disk();

    // 0xE6
    bool unmount_host(uint8_t hs);
    void unmount_host_basic();
    void unmount_host_raw();

    // 0xE5
    uint16_t get_directory_position();
    void get_directory_position_basic();
    void get_directory_position_raw();

    // 0xE4
    bool set_directory_position(uint16_t pos);
    void set_directory_position_basic();
    void set_directory_position_raw();

    // 0xE3
    void set_hindex();

    // 0xE2
    void set_device_filename(uint8_t slot, uint8_t host, uint8_t mode, std::string filename);
    void set_device_filename_basic();
    void set_device_filename_raw();

    // 0xE1
    void set_host_prefix();

    // 0xE0
    void get_host_prefix();
    
    // 0xDF
    void set_external_clock();
    
    // 0xDE
    int write_app_key(std::vector<uint8_t>&& value);
    void write_app_key_basic();
    void write_app_key_raw();

    // 0xDD
    int read_app_key(char *filename, std::vector<uint8_t>& file_data);
    void read_app_key_basic();
    void read_app_key_raw();

    // 0xDC
    void open_app_key(uint16_t creator, uint8_t app, uint8_t key, appkey_mode mode, uint8_t reserved);
    void open_app_key_basic();
    void open_app_key_raw();

    // 0xDB
    void close_app_key();
    void close_app_key_basic();
    void close_app_key_raw();
    
    // 0xDA
    std::string get_device_filename(uint8_t ds);
    void get_device_filename_basic();
    void get_device_filename_raw();

    // 0xD9
    void set_boot_config(bool should_boot_config);
    void set_boot_config_basic();
    void set_boot_config_raw();

    // 0xD8
    void copy_file();

    // 0xD6
    void set_boot_mode(uint8_t boot_device, bool should_boot_config);
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

    device_state_t process() override;

    void shutdown() override;

    int appkey_size = 64;
    std::map<int, int> mode_to_keysize = {
        {0, 64},
        {2, 256}
    };
    bool check_appkey_creator(bool check_is_write);

    /**
     * @brief called to process command either at open or listen
     */
    void iec_command();

    void set_fuji_iec_status(int8_t error, const std::string msg) {
        set_iec_status(error, last_command, msg, fnWiFi.connected(), 15);
    }

public:
    bool boot_config = true;

    bool status_wait_enabled = true;
    
    //iecNetwork *network();

    iecDrive *bootdisk();

    void insert_boot_device(uint8_t d);

    void setup(systemBus *bus);

    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    // 0xD7 - why is this public?
    void mount_all();

    iecFuji();
};

extern iecFuji theFuji;

#endif // FUJI_H