#ifndef FUJI_H
#define FUJI_H

#include <cstdint>

#include "network.h"
#include "disk.h"

#include "fujiHost.h"
#include "fujiDisk.h"

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

class s100spiFuji : public virtualDevice
{
private:
    bool isReady = false;
    bool alreadyRunning = false; // Replace isReady and scanStarted with THIS.
    bool scanStarted = false;
    bool hostMounted[MAX_HOSTS];
    bool setSSIDStarted = false;

    uint8_t response[1024];
    uint16_t response_len;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    int _current_open_directory_slot = -1;

    s100spiDisk *_bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

protected:
    void s100spi_reset_fujinet();          // 0xFF
    void s100spi_net_get_ssid();           // 0xFE
    void s100spi_net_scan_networks();      // 0xFD
    void s100spi_net_scan_result();        // 0xFC
    void s100spi_net_set_ssid(uint16_t s);           // 0xFB
    void s100spi_net_get_wifi_status();    // 0xFA
    void s100spi_mount_host();             // 0xF9
    void s100spi_disk_image_mount();       // 0xF8
    void s100spi_open_directory(uint16_t s);         // 0xF7
    void s100spi_read_directory_entry();   // 0xF6
    void s100spi_close_directory();        // 0xF5
    void s100spi_read_host_slots();        // 0xF4
    void s100spi_write_host_slots();       // 0xF3
    void s100spi_read_device_slots();      // 0xF2
    void s100spi_write_device_slots();     // 0xF1
    void s100spi_disk_image_umount();      // 0xE9
    void s100spi_get_adapter_config();     // 0xE8
    void s100spi_new_disk();               // 0xE7
    void s100spi_unmount_host();           // 0xE6
    void s100spi_get_directory_position(); // 0xE5
    void s100spi_set_directory_position(); // 0xE4
    void s100spi_set_hs100spi_index();         // 0xE3
    void s100spi_set_device_filename(uint16_t s);    // 0xE2
    void s100spi_set_host_prefix();        // 0xE1
    void s100spi_get_host_prefix();        // 0xE0
    void s100spi_set_s100spi_external_clock(); // 0xDF
    void s100spi_write_app_key();          // 0xDE
    void s100spi_read_app_key();           // 0xDD
    void s100spi_open_app_key();           // 0xDC
    void s100spi_close_app_key();          // 0xDB
    void s100spi_get_device_filename();    // 0xDA
    void s100spi_set_boot_config();        // 0xD9
    void s100spi_copy_file();              // 0xD8
    void s100spi_set_boot_mode();          // 0xD6
    void s100spi_enable_device();          // 0xD5
    void s100spi_disable_device();         // 0xD4

    void s100spi_test_command();

    void s100spi_control_status() override;
    void s100spi_control_send();
    void s100spi_control_clr();

    void s100spi_process(uint8_t b) override;

    void shutdown() override;

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    s100spiDisk *bootdisk();

    s100spiNetwork *network();

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

    void mount_all();              // 0xD7

    s100spiFuji();
};

extern s100spiFuji theFuji;

#endif // FUJI_H
