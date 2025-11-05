#ifndef FUJI_H
#define FUJI_H

#include <cstdint>

#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/md5.h"

#include "network.h"
#include "disk.h"

#include "fujiHost.h"
#include "fujiDisk.h"

#include "hash.h"

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
    char ssid[MAX_SSID_LEN+1];
    char hostname[64];
    unsigned char localIP[4];
    unsigned char gateway[4];
    unsigned char netmask[4];
    unsigned char dnsIP[4];
    unsigned char macAddress[6];
    unsigned char bssid[6];
    char fn_version[15];
} __attribute__((packed)) AdapterConfig;

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

class rc2014Fuji : public virtualDevice
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

    rc2014Disk *_bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

    mbedtls_md5_context _md5;
    mbedtls_sha1_context _sha1;
    mbedtls_sha256_context _sha256;
    mbedtls_sha512_context _sha512;

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;

protected:
    void rc2014_reset_fujinet();          // 0xFF
    void rc2014_net_get_ssid();           // 0xFE
    void rc2014_net_scan_networks();      // 0xFD
    void rc2014_net_scan_result();        // 0xFC
    void rc2014_net_set_ssid();           // 0xFB
    void rc2014_net_get_wifi_status();    // 0xFA
    void rc2014_mount_host();             // 0xF9
    void rc2014_disk_image_mount();       // 0xF8
    void rc2014_open_directory();         // 0xF7
    void rc2014_read_directory_entry();   // 0xF6
    void rc2014_close_directory();        // 0xF5
    void rc2014_read_host_slots();        // 0xF4
    void rc2014_write_host_slots();       // 0xF3
    void rc2014_read_device_slots();      // 0xF2
    void rc2014_write_device_slots();     // 0xF1
    void rc2014_disk_image_umount();      // 0xE9
    void rc2014_get_adapter_config();     // 0xE8
    void rc2014_new_disk();               // 0xE7
    void rc2014_unmount_host();           // 0xE6
    void rc2014_get_directory_position(); // 0xE5
    void rc2014_set_directory_position(); // 0xE4
    void rc2014_set_hrc2014_index();      // 0xE3
    void rc2014_set_device_filename();    // 0xE2
    void rc2014_set_host_prefix();        // 0xE1
    void rc2014_get_host_prefix();        // 0xE0
    void rc2014_set_rc2014_external_clock(); // 0xDF
    void rc2014_write_app_key();          // 0xDE
    void rc2014_read_app_key();           // 0xDD
    void rc2014_open_app_key();           // 0xDC
    void rc2014_close_app_key();          // 0xDB
    void rc2014_get_device_filename();    // 0xDA
    void rc2014_set_boot_config();        // 0xD9
    void rc2014_copy_file();              // 0xD8
    void rc2014_set_boot_mode();          // 0xD6
    void rc2014_enable_device();          // 0xD5
    void rc2014_disable_device();         // 0xD4
    void rc2014_device_enabled_status();  // 0xD1
    void rc2014_base64_encode_input();    // 0xD0
    void rc2014_base64_encode_compute();  // 0xCF
    void rc2014_base64_encode_length();   // 0xCE
    void rc2014_base64_encode_output();   // 0xCD
    void rc2014_base64_decode_input();    // 0xCC
    void rc2014_base64_decode_compute();  // 0xCB
    void rc2014_base64_decode_length();   // 0xCA
    void rc2014_base64_decode_output();   // 0xC9
    void rc2014_hash_input();             // 0xC8
    void rc2014_hash_compute(bool clear_data); // 0xC7, 0xC3
    void rc2014_hash_length();            // 0xC6
    void rc2014_hash_output();            // 0xC5
    void rc2014_hash_clear();             // 0xC2

    // TODO
    // void rc2014_get_adapter_config_extended(); // 0xC4

    void rc2014_test_command();

    void rc2014_control_status() override;
    void rc2014_control_send();
    void rc2014_control_clr();

    void rc2014_process(uint32_t commanddata, uint8_t checksum) override;

    void shutdown() override;

public:
    bool boot_config = true;

    bool status_wait_enabled = true;

    rc2014Disk *bootdisk();

    rc2014Network *network();

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

    rc2014Fuji();
};

extern rc2014Fuji theFuji;

#endif // FUJI_H
