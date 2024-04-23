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

enum appkey_mode : uint8_t
{
    APPKEYMODE_READ = 0,
    APPKEYMODE_WRITE,
    APPKEYMODE_INVALID
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

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

#ifdef ESP_PLATFORM
    drivewireCassette _cassetteDev;
#endif

    int _current_open_directory_slot = -1;

    drivewireDisk _bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void reset_fujinet();          // 0xFF
    void net_get_ssid();           // 0xFE
    void net_scan_networks();      // 0xFD
    void net_scan_result();        // 0xFC
    void net_set_ssid();           // 0xFB
    void net_get_wifi_status();    // 0xFA
    void mount_host();             // 0xF9
    void disk_image_mount();       // 0xF8
    void open_directory();         // 0xF7
    void read_directory_entry();   // 0xF6
    void close_directory();        // 0xF5
    void read_host_slots();        // 0xF4
    void write_host_slots();       // 0xF3
    void read_device_slots();      // 0xF2
    void write_device_slots();     // 0xF1
    void enable_udpstream();       // 0xF0
    void net_get_wifi_enabled();   // 0xEA
    void disk_image_umount();      // 0xE9
    void get_adapter_config();     // 0xE8
    void new_disk();               // 0xE7
    void unmount_host();           // 0xE6
    void get_directory_position(); // 0xE5
    void set_directory_position(); // 0xE4
    void set_hdrivewire_index();         // 0xE3
    void set_device_filename();    // 0xE2
    void set_host_prefix();        // 0xE1
    void get_host_prefix();        // 0xE0
    void set_drivewire_external_clock(); // 0xDF
    void write_app_key();          // 0xDE
    void read_app_key();           // 0xDD
    void open_app_key();           // 0xDC
    void close_app_key();          // 0xDB
    void get_device_filename();    // 0xDA
    void set_boot_config();        // 0xD9
    void copy_file();              // 0xD8
    void set_boot_mode();          // 0xD6
    void device_error();           // 0x00

    void shutdown() override;

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

    void process();

    void mount_all();              // 0xD7

    drivewireFuji();
};

extern drivewireFuji theFuji;

#endif // FUJI_H

