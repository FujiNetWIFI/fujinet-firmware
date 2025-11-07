#ifndef FUJI_H
#define FUJI_H

#include <cstdint>

#include "network.h"
#include "disk.h"
#include "serial.h"

#include "fujiHost.h"
#include "fujiDisk.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 4

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

class adamFuji : public virtualDevice
{
private:
    bool new_disk_completed = false;
    bool isReady = false;
    bool alreadyRunning = false; // Replace isReady and scanStarted with THIS.
    bool scanStarted = false;
    bool hostMounted[MAX_HOSTS];
    bool setSSIDStarted = false;
    unsigned char active_rotate_slot=0;

    uint8_t response[1024];
    uint16_t response_len = 0;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    adamDisk *_bootDisk = nullptr; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void adamnet_reset_fujinet();          // 0xFF
    void adamnet_net_get_ssid();           // 0xFE
    void adamnet_net_scan_networks();      // 0xFD
    void adamnet_net_scan_result();        // 0xFC
    void adamnet_net_set_ssid(uint16_t s);           // 0xFB
    void adamnet_net_get_wifi_status();    // 0xFA
    void adamnet_mount_host();             // 0xF9
    void adamnet_disk_image_mount();       // 0xF8
    void adamnet_open_directory(uint16_t s);         // 0xF7
    void adamnet_read_directory_entry();   // 0xF6
    void adamnet_close_directory();        // 0xF5
    void adamnet_read_host_slots();        // 0xF4
    void adamnet_write_host_slots();       // 0xF3
    void adamnet_read_device_slots();      // 0xF2
    void adamnet_write_device_slots();     // 0xF1
    void adamnet_disk_image_umount();      // 0xE9
    void adamnet_get_adapter_config();     // 0xE8
    void adamnet_new_disk();               // 0xE7
    void adamnet_unmount_host();           // 0xE6
    void adamnet_get_directory_position(); // 0xE5
    void adamnet_set_directory_position(); // 0xE4
    void adamnet_set_hadamnet_index();         // 0xE3
    void adamnet_set_device_filename(uint16_t s);    // 0xE2
    void adamnet_set_host_prefix();        // 0xE1
    void adamnet_get_host_prefix();        // 0xE0
    void adamnet_set_adamnet_external_clock(); // 0xDF
    void adamnet_write_app_key();          // 0xDE
    void adamnet_read_app_key();           // 0xDD
    void adamnet_open_app_key();           // 0xDC
    void adamnet_close_app_key();          // 0xDB
    void adamnet_get_device_filename();    // 0xDA
    void adamnet_set_boot_config();        // 0xD9
    void adamnet_copy_file();              // 0xD8
    void adamnet_set_boot_mode();          // 0xD6
    void adamnet_enable_device();          // 0xD5
    void adamnet_disable_device();         // 0xD4
    void adamnet_random_number();          // 0xD3
    void adamnet_get_time();               // 0xD2
    void adamnet_device_enable_status();   // 0xD1
    void adamnet_get_copy_status();        // 0xD0

    void adamnet_test_command();

    void adamnet_control_status() override;
    void adamnet_control_send();
    void adamnet_control_clr();

    void adamnet_process(uint8_t b) override;

    void shutdown() override;

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    adamDisk *bootdisk();

    adamNetwork *network();

    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup();

    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    void mount_all();

    adamFuji();

    std::string copySpec;
    unsigned char sourceSlot = 0;
    unsigned char destSlot = 0;
    std::string sourcePath;
    std::string destPath;
    FILE *sourceFile = nullptr;
    FILE *destFile = nullptr;
    char *dataBuf = nullptr;
    TaskHandle_t copy_task_handle;
};

extern adamFuji theFuji;
extern adamSerial *theSerial;

#endif // FUJI_H
