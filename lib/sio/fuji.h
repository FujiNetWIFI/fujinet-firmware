#ifndef FUJI_H
#define FUJI_H
#include <cstdint>

#include "../../include/debug.h"
#include "sio.h"
#include "network.h"
#include "cassette.h"

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

class sioFuji : public sioDevice
{
private:
    sioBus *_sio_bus;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    sioCassette _cassetteDev;

    int _current_open_directory_slot = -1;

    sioDisk _bootDisk; // special disk drive just for configuration

    uint8_t _countScannedSSIDs = 0;

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    appkey _current_appkey;

protected:
    void sio_reset_fujinet();          // 0xFF
    void sio_net_get_ssid();           // 0xFE
    void sio_net_scan_networks();      // 0xFD
    void sio_net_scan_result();        // 0xFC
    void sio_net_set_ssid();           // 0xFB
    void sio_net_get_wifi_status();    // 0xFA
    void sio_mount_host();             // 0xF9
    void sio_disk_image_mount();       // 0xF8
    void sio_open_directory();         // 0xF7
    void sio_read_directory_entry();   // 0xF6
    void sio_close_directory();        // 0xF5
    void sio_read_host_slots();        // 0xF4
    void sio_write_host_slots();       // 0xF3
    void sio_read_device_slots();      // 0xF2
    void sio_write_device_slots();     // 0xF1
    void sio_disk_image_umount();      // 0xE9
    void sio_get_adapter_config();     // 0xE8
    void sio_new_disk();               // 0xE7
    void sio_unmount_host();           // 0xE6
    void sio_get_directory_position(); // 0xE5
    void sio_set_directory_position(); // 0xE4
    void sio_set_hsio_index();         // 0xE3
    void sio_set_device_filename();    // 0xE2
    void sio_set_host_prefix();        // 0xE1
    void sio_get_host_prefix();        // 0xE0
    void sio_set_sio_external_clock(); // 0xDF
    void sio_write_app_key();          // 0xDE
    void sio_read_app_key();           // 0xDD
    void sio_open_app_key();           // 0xDC
    void sio_close_app_key();          // 0xDB
    void sio_get_device_filename();    // 0xDA
    void sio_set_boot_config();        // 0xD9

    void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

    void shutdown() override;

public:
    bool boot_config = true;
    sioDisk *bootdisk();

    sioNetwork *network();

    sioCassette *cassette() { return &_cassetteDev; };
    void debug_tape();

    void setup(sioBus *siobus);
    void image_rotate();

    sioFuji();
};

extern sioFuji theFuji;

#endif // FUJI_H
