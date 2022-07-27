#ifndef FUJI_H
#define FUJI_H

#include <cstdint>
#include <cstring>

#include "bus.h"
//#include "network.h"
//#include "cassette.h"

#include "fujiHost.h"
#include "fujiDisk.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 8

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

class fujiDevice : public iecDevice
{
private:
    iecBus *_bus;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

//    iecCassette _cassetteDev;

    int _current_open_directory_slot = -1;

    iecDisk _bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void bus_reset_fujinet();          // 0xFF
    void bus_net_get_ssid();           // 0xFE
    void bus_net_scan_networks();      // 0xFD
    void bus_net_scan_result();        // 0xFC
    void bus_net_set_ssid();           // 0xFB
    void bus_net_get_wifi_status();    // 0xFA
    void bus_mount_host();             // 0xF9
    void bus_disk_image_mount();       // 0xF8
    void bus_open_directory();         // 0xF7
    void bus_read_directory_entry();   // 0xF6
    void bus_close_directory();        // 0xF5
    void bus_read_host_slots();        // 0xF4
    void bus_write_host_slots();       // 0xF3
    void bus_read_device_slots();      // 0xF2
    void bus_write_device_slots();     // 0xF1
    void bus_disk_image_umount();      // 0xE9
    void bus_get_adapter_config();     // 0xE8
    void bus_new_disk();               // 0xE7
    void bus_unmount_host();           // 0xE6
    void bus_get_directory_position(); // 0xE5
    void bus_set_directory_position(); // 0xE4
    void bus_set_hbus_index();         // 0xE3
    void bus_set_device_filename();    // 0xE2
    void bus_set_host_prefix();        // 0xE1
    void bus_get_host_prefix();        // 0xE0
    void bus_set_bus_external_clock(); // 0xDF
    void bus_write_app_key();          // 0xDE
    void bus_read_app_key();           // 0xDD
    void bus_open_app_key();           // 0xDC
    void bus_close_app_key();          // 0xDB
    void bus_get_device_filename();    // 0xDA
    void bus_set_boot_config();        // 0xD9
    void bus_copy_file();              // 0xD8
    void bus_set_boot_mode();          // 0xD6

    void bus_status() override;
    void bus_process(uint32_t commanddata, uint8_t checksum) override;

    void shutdown() override;

public:
    bool boot_config = true;

    bool status_wait_enabled = true;
    
    iecDisk *bootdisk();

//    iecNetwork *network();

//    iecCassette *cassette() { return &_cassetteDev; };
//    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup(iecBus *iecbus);

    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    void bus_mount_all();              // 0xD7

    fujiDevice();
};

extern fujiDevice theFuji;

#endif // FUJI_H