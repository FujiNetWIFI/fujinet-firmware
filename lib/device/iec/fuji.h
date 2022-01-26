#ifndef FUJI_H
#define FUJI_H
#include <cstdint>

#include "../../include/debug.h"

#include "../../bus/bus.h"

#include "../../fuji/fujiHost.h"
#include "../../fuji/fujiDisk.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 4

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
    bool isReady = false;
    bool alreadyRunning = false; // Replace isReady and scanStarted with THIS.
    bool scanStarted = false;
    bool hostMounted[MAX_HOSTS];
    bool setSSIDStarted = false;

    uint8_t response[1024];
    uint16_t response_len;

    iecBus *_iec_bus;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    iecDisk *_bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void iec_reset_fujinet();          // 0xFF
    void iec_net_get_ssid();           // 0xFE
    void iec_net_scan_networks();      // 0xFD
    void iec_net_scan_result();        // 0xFC
    void iec_net_set_ssid(uint16_t s);           // 0xFB
    void iec_net_get_wifi_status();    // 0xFA
    void iec_mount_host();             // 0xF9
    void iec_disk_image_mount();       // 0xF8
    void iec_open_directory(uint16_t s);         // 0xF7
    void iec_read_directory_entry();   // 0xF6
    void iec_close_directory();        // 0xF5
    void iec_read_host_slots();        // 0xF4
    void iec_write_host_slots();       // 0xF3
    void iec_read_device_slots();      // 0xF2
    void iec_write_device_slots();     // 0xF1
    void iec_disk_image_umount();      // 0xE9
    void iec_get_adapter_config();     // 0xE8
    void iec_new_disk();               // 0xE7
    void iec_unmount_host();           // 0xE6
    void iec_get_directory_position(); // 0xE5
    void iec_set_directory_position(); // 0xE4
    void iec_set_hiec_index();         // 0xE3
    void iec_set_device_filename(uint16_t s);    // 0xE2
    void iec_set_host_prefix();        // 0xE1
    void iec_get_host_prefix();        // 0xE0
    void iec_set_iec_external_clock(); // 0xDF
    void iec_write_app_key();          // 0xDE
    void iec_read_app_key();           // 0xDD
    void iec_open_app_key();           // 0xDC
    void iec_close_app_key();          // 0xDB
    void iec_get_device_filename();    // 0xDA
    void iec_set_boot_config();        // 0xD9
    void iec_copy_file();              // 0xD8
    void iec_set_boot_mode();          // 0xD6
    void iec_enable_device();          // 0xD5
    void iec_disable_device();         // 0xD4

    void iec_test_command();

    void iec_control_status();
    void iec_control_send();
    void iec_control_clr();

    void iec_process(uint8_t b);

    void shutdown() override;

public:
    bool boot_config = true;
    
    bool status_wait_enabled = true;
    
    iecDisk *bootdisk();

//    iecNetwork *network();

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

    void iec_mount_all();              // 0xD7
    void sio_mount_all();

    iecFuji();
};

extern iecFuji theFuji;

#endif // FUJI_H
