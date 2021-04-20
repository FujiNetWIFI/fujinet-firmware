#ifndef IEC_FUJI_H
#define IEC_FUJI_H
#include <cstdint>

#include "../../include/debug.h"
#include "iecBus.h"
#include "iecDisk.h"
#include "iecApeTime.h"
#include "iecVoice.h"

#include "network.h"
#include "cassette.h"

#include "modem.h"
#include "printerlist.h"
#include "midimaze.h"
#include "siocpm.h"
#include "samlib.h"

#include "fujiHost.h"
#include "fujiDisk.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 8

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define MAX_APPKEY_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

typedef struct
{
    char ssid[32];
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

class iecFuji : public iecDevice
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
    void _reset_fujinet();          // 0xFF
    void _net_get_ssid();           // 0xFE
    void _net_scan_networks();      // 0xFD
    void _net_scan_result();        // 0xFC
    void _net_set_ssid();           // 0xFB
    void _net_get_wifi_status();    // 0xFA
    void _mount_host();             // 0xF9
    void _disk_image_mount();       // 0xF8
    void _open_directory();         // 0xF7
    void _read_directory_entry();   // 0xF6
    void _close_directory();        // 0xF5
    void _read_host_slots();        // 0xF4
    void _write_host_slots();       // 0xF3
    void _read_device_slots();      // 0xF2
    void _write_device_slots();     // 0xF1
    void _disk_image_umount();      // 0xE9
    void _get_adapter_config();     // 0xE8
    void _new_disk();               // 0xE7
    void _unmount_host();           // 0xE6
    void _get_directory_position(); // 0xE5
    void _set_directory_position(); // 0xE4
    void _set_hsio_index();         // 0xE3
    void _set_device_filename();    // 0xE2
    void _set_host_prefix();        // 0xE1
    void _get_host_prefix();        // 0xE0
    void _write_app_key();          // 0xDE
    void _read_app_key();           // 0xDD
    void _open_app_key();           // 0xDC
    void _close_app_key();          // 0xDB
    void _get_device_filename();    // 0xDA
    void _set_boot_config();        // 0xD9
    void _copy_file();              // 0xD8
    void _set_boot_mode();          // 0xD6

    void _status() override;
    void _process() override;

    void shutdown() override;

public:
    bool boot_config = true;
    iecDisk *bootdisk();

    iecNetwork *network();

//    iecCassette *cassette() { return &_cassetteDev; };
    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup(iecBus *iecbus);

    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    void _mount_all();              // 0xD7

    iecFuji();
};

#ifdef BUILD_CBM
extern iecFuji theFuji;
#endif

#endif // IEC_FUJI_H
