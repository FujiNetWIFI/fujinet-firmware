#ifndef FUJIDEVICE_H
#define FUJIDEVICE_H

#include "fnConfig.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"
#include "../fuji/fujiCmd.h"

#include "hash.h"

#include <string>
#include <optional>

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

typedef struct {
    uint16_t avail;
    uint8_t conn, err;
} NDeviceStatus;

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

    void fujicmd_read_directory_block(uint8_t num_pages, uint8_t group_size);

protected:
    fujiHost _fnHosts[MAX_HOSTS];
    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    appkey _current_appkey;
    int _current_open_directory_slot = -1;
    uint8_t _countScannedSSIDs = 0;

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;

    virtual void transaction_complete() = 0;
    virtual void transaction_error() = 0;
    virtual bool transaction_get(void *data, size_t len) = 0;
    virtual void transaction_put(const void *data, size_t len, bool err=false) = 0;

    virtual size_t setDirEntryDetails(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen) = 0;

    // ============ Validation of inputs ============
    bool validate_host_slot(uint8_t slot, const char *dmsg=nullptr);
    bool validate_device_slot(uint8_t slot, const char *dmsg = nullptr);

public:
    bool boot_config = true;
    DEVICE_TYPE bootdisk; // special disk drive just for configuration

    fujiDevice();
    virtual void setup() = 0;
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
    // fujicmd_ methods are the public entry points for handling
    // commands received over the bus. Intended for ease of calling by
    // process() because they validate inputs, call the matching
    // fujicore_ logic, and send the result back via transaction_*.
    virtual bool fujicmd_mount_all_success();
    virtual void fujicmd_reset();
    bool fujicmd_mount_host_success(unsigned hostSlot);
    void fujicmd_net_scan_networks();
    void fujicmd_net_scan_result(uint8_t index);
    void fujicmd_net_get_ssid();
    bool fujicmd_net_set_ssid_success(const char *ssid, const char *password, bool save);
    void fujicmd_net_get_wifi_enabled();
    bool fujicmd_disk_image_mount_success(uint8_t deviceSlot, uint8_t access_mode);
    bool fujicmd_disk_image_unmount_success(uint8_t deviceSlot);
    void fujicmd_image_rotate();
    bool fujicmd_open_directory_success(uint8_t hostSlot, char *dirpath, uint16_t bufsize);
    virtual void fujicmd_close_directory();
    virtual void fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl);
    void fujicmd_get_directory_position();
    void fujicmd_set_directory_position(uint16_t pos);
    bool fujicmd_copy_file_success(uint8_t sourceSlot, uint8_t destSlot, std::string copySpec);
    void fujicmd_get_adapter_config();
    void fujicmd_get_adapter_config_extended();
    void fujicmd_get_device_filename(uint8_t slot);
    bool fujicmd_set_device_filename_success(uint8_t deviceSlot, uint8_t host, uint8_t mode);
    void fujicmd_get_host_prefix(uint8_t hostSlot);
    void fujicmd_net_get_wifi_status();
    void fujicmd_read_host_slots();
    void fujicmd_write_host_slots();
    void fujicmd_set_boot_config(bool enable);
    void fujicmd_set_boot_mode(uint8_t bootMode, std::string extension,
                               mediatype_t disk_type, DEVICE_TYPE *disk_dev);
    void fujicmd_set_host_prefix(uint8_t hostSlot, const char *prefix=nullptr);
    bool fujicmd_unmount_host_success(uint8_t hostSlot);
    void fujicmd_read_device_slots(uint8_t numDevices);
    void fujicmd_write_device_slots(uint8_t numDevices);
    void fujicmd_status();
    void fujicmd_set_sio_external_clock(uint16_t speed);
#ifdef SYSTEM_BUS_IS_UDP
    void fujicmd_enable_udpstream(int port);
#endif /* SYSTEM_BUS_IS_UDP */

    // Move appkey stuff to its own file?
    void fujicmd_open_app_key();
    void fujicmd_close_app_key();
    void fujicmd_write_app_key(uint16_t keylen);
    void fujicmd_read_app_key();

    // ============ Implementations by fujicmd_ methods ============
    // These are safe to call directly if the bus abstraction
    // (transaction_) doesn't suit the platform.
    void fujicore_open_app_key(uint16_t creator, uint8_t app, uint8_t key,
                               appkey_mode mode, uint8_t reserved);
    SSIDInfo fujicore_net_scan_result(uint8_t index, bool *err=nullptr);
    SSIDConfig fujicore_net_get_ssid();
    uint8_t fujicore_net_get_wifi_status();
    uint8_t fujicore_net_get_wifi_enabled();
    int fujicore_write_app_key(std::vector<uint8_t>&& value, int *err=nullptr);
    std::optional<std::vector<uint8_t>> fujicore_read_app_key();
    bool fujicore_open_directory_success(uint8_t hostSlot, std::string dirpath,
                                         std::string pattern);
    std::optional<std::string> fujicore_read_directory_entry(size_t maxlen, uint8_t addtl);
    uint16_t fujicore_get_directory_position();
    AdapterConfigExtended fujicore_get_adapter_config_extended();
    bool fujicore_set_device_filename_success(uint8_t deviceSlot, uint8_t host,
                                              uint8_t mode, std::string filename);
    std::optional<std::string> fujicore_get_device_filename(uint8_t slot);
    bool fujicore_disk_image_mount_success(uint8_t deviceSlot, uint8_t access_mode);

    // Should be protected but directly accessed by sio.cpp
    bool status_wait_enabled = true;

    // Should be protected but being called by drivewire.cpp
    void insert_boot_device(uint8_t image_id, std::string extension,
                            mediatype_t disk_type, DEVICE_TYPE *disk_dev);
};

extern fujiDevice *theFuji;

#endif // FUJIDEVICE_H
