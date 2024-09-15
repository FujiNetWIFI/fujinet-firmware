#ifndef FUJI_H
#define FUJI_H

#include <cstdint>
#include <cstring>
#include <compat_string.h>
#include "bus.h"
#include "disk.h"
#include "network.h"
#include "cassette.h"

#include "fujiHost.h"
#include "fujiDisk.h"
#include "fujiCmd.h"

#include "hash.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 4
#define MAX_NETWORK_DEVICES 8

#ifdef ESP32_PLATFORM
#else
#define ESP_OK  0
#endif

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define MAX_APPKEY_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

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

class drivewireFuji : public virtualDevice
{
private:
    systemBus *_drivewire_bus = nullptr;

    bool wifiScanStarted = false;

    char dirpath[256];

    std::string response;

    uint8_t errorCode;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;

#ifdef ESP_PLATFORM
    drivewireCassette _cassetteDev;
#endif

    int _current_open_directory_slot = -1;

    drivewireDisk _bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

    void resetState();
    uint16_t base64_encode_length_var;
    uint16_t base64_decode_length_var;
    uint16_t hash_length_var;

    
protected:
    int reset_fujinet(std::vector<uint8_t> *);          // 0xFF
    int net_get_ssid(std::vector<uint8_t> *);           // 0xFE
    int net_scan_networks(std::vector<uint8_t> *);      // 0xFD
    int net_scan_result(std::vector<uint8_t> *);        // 0xFC
    int net_set_ssid(std::vector<uint8_t> *);           // 0xFB
    int net_get_wifi_status(std::vector<uint8_t> *);    // 0xFA
    int mount_host(std::vector<uint8_t> *);             // 0xF9
    int disk_image_mount(std::vector<uint8_t> *);       // 0xF8
    int open_directory(std::vector<uint8_t> *);         // 0xF7
    int read_directory_entry(std::vector<uint8_t> *);   // 0xF6
    int close_directory(std::vector<uint8_t> *);        // 0xF5
    int read_host_slots(std::vector<uint8_t> *);        // 0xF4
    int write_host_slots(std::vector<uint8_t> *);       // 0xF3
    int read_device_slots(std::vector<uint8_t> *);      // 0xF2
    int write_device_slots(std::vector<uint8_t> *);     // 0xF1
    int enable_udpstream(std::vector<uint8_t> *);       // 0xF0
    int net_get_wifi_enabled(std::vector<uint8_t> *);   // 0xEA
    int disk_image_umount(std::vector<uint8_t> *);      // 0xE9
    int get_adapter_config(std::vector<uint8_t> *);     // 0xE8
    int new_disk(std::vector<uint8_t> *);               // 0xE7
    int unmount_host(std::vector<uint8_t> *);           // 0xE6
    int get_directory_position(std::vector<uint8_t> *); // 0xE5
    int set_directory_position(std::vector<uint8_t> *); // 0xE4
    int set_hdrivewire_index(std::vector<uint8_t> *);   // 0xE3
    int set_device_filename(std::vector<uint8_t> *);    // 0xE2
    int set_host_prefix(std::vector<uint8_t> *);        // 0xE1
    int get_host_prefix(std::vector<uint8_t> *);        // 0xE0
    int set_drivewire_external_clock(std::vector<uint8_t> *); // 0xDF
    int write_app_key(std::vector<uint8_t> *);          // 0xDE
    int read_app_key(std::vector<uint8_t> *);           // 0xDD
    int open_app_key(std::vector<uint8_t> *);           // 0xDC
    int close_app_key(std::vector<uint8_t> *);          // 0xDB
    int get_device_filename(std::vector<uint8_t> *);    // 0xDA
    int set_boot_config(std::vector<uint8_t> *);        // 0xD9
    int copy_file(std::vector<uint8_t> *);              // 0xD8
    int set_boot_mode(std::vector<uint8_t> *);          // 0xD6
    int random(std::vector<uint8_t> *);                 // 0xD3
    int base64_encode_input(std::vector<uint8_t> *);    // 0xD0
    int base64_encode_input_p2(std::vector<uint8_t> *q);
    int base64_encode_compute(std::vector<uint8_t> *);  // 0xCF
    int base64_encode_length(std::vector<uint8_t> *);   // 0xCE
    int base64_encode_output(std::vector<uint8_t> *);   // 0xCD
    int base64_decode_input(std::vector<uint8_t> *);    // 0xCC
    int base64_decode_input_p2(std::vector<uint8_t> *);    // 0xCC
    int base64_decode_compute(std::vector<uint8_t> *);  // 0xCB
    int base64_decode_length(std::vector<uint8_t> *);   // 0xCA
    int base64_decode_output(std::vector<uint8_t> *);   // 0xC9
    int hash_input(std::vector<uint8_t> *);             // 0xC8
    int hash_input_p2(std::vector<uint8_t> *);             // 0xC8
    int state_hash_compute_true(std::vector<uint8_t> *); // 0xC7, 0xC3
    int state_hash_compute_false(std::vector<uint8_t> *); // 0xC7, 0xC3
    int hash_length(std::vector<uint8_t> *);            // 0xC6
    int hash_output(std::vector<uint8_t> *);            // 0xC5
    int get_adapter_config_extended(std::vector<uint8_t> *); // 0xC4
    int hash_clear(std::vector<uint8_t> *);             // 0xC2
    int op_unhandled(std::vector<uint8_t> *q);

    int send_error(std::vector<uint8_t> *q);             // 0x02
    int send_response(std::vector<uint8_t> *);          // 0x01
    int ready(std::vector<uint8_t> *);                  // 0x00
    void shutdown(void) override;

    void hash_compute(uint8_t value, bool clear_data); // 0xC7, 0xC3

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    drivewireDisk *bootdisk();

    drivewireNetwork *network();

#ifdef ESP_PLATFORM
    drivewireCassette *cassette() { return &_cassetteDev; };
#endif
    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup(systemBus *drivewirebus);

    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    int process(std::vector<uint8_t> *q);

    int local_mount_all(std::vector<uint8_t> *q);              // 0xD7

    void mount_all();              // 0xD7

    drivewireFuji();
};

extern drivewireFuji theFuji;

#endif // FUJI_H

