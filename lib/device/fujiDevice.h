#ifndef FUJIDEVICE_H
#define FUJIDEVICE_H

#include "fnConfig.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"
#include "../fuji/fujiCmd.h"

#include "hash.h"

#include <string>

#define MAX_HOSTS MAX_HOST_SLOTS
#define MAX_DISK_DEVICES MAX_MOUNT_SLOTS
#define MAX_NETWORK_DEVICES 8

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define MAX_APPKEY_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

typedef struct
{
    char ssid[MAX_SSID_LEN + 1];
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

typedef struct
{
    char ssid[MAX_SSID_LEN + 1];
    uint8_t rssi;
} SSIDInfo;

typedef struct
{
    char ssid[MAX_SSID_LEN + 1];
    char password[MAX_WIFI_PASS_LEN];
} SSIDConfig;

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

struct disk_slot
{
    uint8_t hostSlot;
    uint8_t mode;
    char filename[MAX_DISPLAY_FILENAME_LEN];
};

class fujiDevice : public virtualDevice
{
private:
    bool hostMounted[MAX_HOSTS];

    void fujicmd_read_directory_block();
    
protected:
    fujiHost _fnHosts[MAX_HOSTS];
    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    systemBus *_bus;
    appkey _current_appkey;
    int _current_open_directory_slot = -1;
    uint8_t _countScannedSSIDs = 0;

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;

    virtual void transaction_complete() = 0;
    virtual void transaction_error() = 0;
    virtual bool transaction_get(void *data, size_t len) = 0;
    virtual void transaction_put(void *data, size_t len, bool err=false) = 0;

    // ============ Validation of inputs ============
    bool validate_host_slot(uint8_t slot, const char *dmsg=nullptr);
    bool validate_device_slot(uint8_t slot, const char *dmsg = nullptr);

    void insert_boot_device(uint8_t image_id, std::string extension, mediatype_t disk_type);

public:
    bool boot_config = true;
    DEVICE_TYPE bootdisk; // special disk drive just for configuration

    fujiDevice();
    virtual void setup(systemBus *sysbus) = 0;
    void shutdown() override;

    fujiHost *get_host(int i) { return &_fnHosts[i]; }
    std::string get_host_prefix(int host_slot) { return _fnHosts[host_slot].get_prefix(); }

    fujiDisk *get_disk(int i) { return &_fnDisks[i]; }
    virtual DEVICE_TYPE *get_disk_dev(int i) { return &_fnDisks[i].disk_dev; }
    int get_disk_id(int drive_slot) { return _fnDisks[drive_slot].disk_dev.id(); }

    void populate_slots_from_config();
    void populate_config_from_slots();
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    // ============ Standard Fuji commands ============
    virtual bool fujicmd_mount_all();
    virtual void fujicmd_reset();
    bool fujicmd_mount_host(unsigned hostSlot);
    void fujicmd_net_scan_networks();
    void fujicmd_net_scan_result(uint8_t index);
    void fujicmd_net_get_ssid();
    bool fujicmd_net_set_ssid(const char *ssid, const char *password, bool save);
    void fujicmd_net_get_wifi_enabled();
    bool fujicmd_disk_image_mount(uint8_t deviceSlot, uint8_t options);
    void fujicmd_image_rotate();
    void fujicmd_open_directory();
    virtual void fujicmd_close_directory();
    void fujicmd_read_directory_entry(uint8_t maxlen, uint8_t aux2);
    bool fujicmd_copy_file(uint8_t sourceSlot, uint8_t destSlot, std::string copySpec);
    void fujicmd_disk_image_umount(uint8_t deviceSlot);
    void fujicmd_get_adapter_config();
    void fujicmd_get_adapter_config_extended();
    void fujicmd_get_device_filename(uint8_t slot);
    bool fujicmd_set_device_filename(uint8_t deviceSlot, uint8_t host, uint8_t mode);
    void fujicmd_get_directory_position();
    void fujicmd_get_host_prefix(uint8_t hostSlot);
    void fujicmd_net_get_wifi_status();
    void fujicmd_read_host_slots();
    void fujicmd_write_host_slots();
    void fujicmd_set_boot_config(bool enable);
    void fujicmd_set_boot_mode(uint8_t bootMode, std::string extension, mediatype_t disk_type);
    void fujicmd_set_directory_position(uint16_t pos);
    void fujicmd_set_host_prefix(uint8_t hostSlot);
    void fujicmd_unmount_host(uint8_t hostSlot);
    void fujicmd_read_device_slots(uint8_t numDevices);
    void fujicmd_write_device_slots(uint8_t numDevices);
    void fujicmd_status();
    void fujicmd_set_sio_external_clock(uint16_t speed);
    void fujicmd_enable_udpstream(int port);

    // Move appkey stuff to its own file?
    void fujicmd_open_app_key();
    void fujicmd_close_app_key();
    void fujicmd_write_app_key(uint16_t keylen);
    void fujicmd_read_app_key();

    // Should be protected but directly accessed by sio.cpp
    bool status_wait_enabled = true;
};

extern fujiDevice *theFuji;

#endif // FUJIDEVICE_H
