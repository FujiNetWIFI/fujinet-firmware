#ifndef FUJIDEVICE_H
#define FUJIDEVICE_H

#include "fnConfig.h"
#include "Base64Mixin.h"
#include "HashMixin.h"
#include "QRMixin.h"
#include "AppKeyMixin.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"

#include <string>
#include <optional>
#include <map>
#include <atomic>
#include <functional>

#define MAX_HOSTS MAX_HOST_SLOTS
#define MAX_DISK_DEVICES MAX_MOUNT_SLOTS
#define MAX_NETWORK_DEVICES 8

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

#define STATUS_MOUNT_TIME       0x01

#define FUJI_BOOTDISK get_disk_dev(0)

#define ADAPTER_CONFIG_FIELDS \
    char ssid[33]; \
    char hostname[64]; \
    unsigned char localIP[4]; \
    unsigned char gateway[4]; \
    unsigned char netmask[4]; \
    unsigned char dnsIP[4]; \
    unsigned char macAddress[6]; \
    unsigned char bssid[6]; \
    char fn_version[15]

typedef struct
{
    ADAPTER_CONFIG_FIELDS;
} __attribute__((packed)) AdapterConfig;

typedef struct
{
    ADAPTER_CONFIG_FIELDS;
    char sLocalIP[16];
    char sGateway[16];
    char sNetmask[16];
    char sDnsIP[16];
    char sMacAddress[18];
    char sBssid[18];
} __attribute__((packed)) AdapterConfigExtended;

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

struct disk_slot
{
    uint8_t hostSlot;
    uint8_t mode;
    char filename[MAX_DISPLAY_FILENAME_LEN];
};

// For some reason the directory entry structure that is sent back
// isn't standardized across platforms. We'll define a common struct
// here with all the fields and the individual platforms can define
// their own customized versions.

struct dirEntryTimestamp {
    uint8_t year, month, day;
    uint8_t hour, minute, second;
} __attribute__((packed));

struct dirEntryDetails {
    dirEntryTimestamp modified;
    uint32_t size;
    uint8_t flags;
    uint8_t mediatype;
};

// File flags
enum DET_file_flags_t {
    DET_FF_DIR   = 0x01,
    DET_FF_TRUNC = 0x02,
};

/* Mixin handling. This allows adding additional commands to a
   fujiDevice without having to mess around with the command handling
   in each subclass. Just add a mixin and add more commands.
 */

template<typename T>
concept FujiPacketLike = requires(T p) {
    { p.device() };
    { p.command() };
    { p.param(0) };
    { p.data() };
    { p.dataAsString() };
};

static_assert(FujiPacketLike<FUJI_COMMAND_PACKET>,
              "FUJI_COMMAND_PACKET must satisfy FujiPacketLike");

using FujiCommandHandlers = std::unordered_map<
    fujiCommandID_t,
    std::function<void(const FUJI_COMMAND_PACKET &)>
    >;

// This class inherits from all the mixins you list and tries each one in order
template<typename... FujiDeviceMixins>
requires FujiPacketLike<FUJI_COMMAND_PACKET>
class FujiDeviceChain : public FujiDeviceMixins...
{
 protected:
    bool tryAllMixins(const FUJI_COMMAND_PACKET &packet) {
        // Try each mixin's processCommand() until one returns true
        return (FujiDeviceMixins::processCommand(packet) || ...);
    }
    bool checkAllMixins(fujiCommandID_t command) {
        // Try each mixin's processCommand() until one returns true
        return (FujiDeviceMixins::recognizesCommand(command) || ...);
    }

 public:
    bool processCommand(const FUJI_COMMAND_PACKET &packet) override {
        return (FujiDeviceMixins::processCommand(packet) || ...);
    }
};

class fujiDevice : public virtual virtualDevice,
                   public FujiDeviceChain<Base64Mixin, HashMixin, QRMixin, AppKeyMixin>
{
private:
    FujiCommandHandlers handlers;
    bool hostMounted[MAX_HOSTS];

    void fujicmd_read_directory_block(uint8_t num_pages, uint8_t group_size);

protected:
    fujiHost _fnHosts[MAX_HOSTS];
    fujiDisk _fnDisks[MAX_DISK_DEVICES];
    const unsigned int _totalDiskDevices;
    const std::string _diskImageExtension;
    const std::optional<std::string> _lobbyDiskURL;

    std::map<int, int> mode_to_keysize = {
        {0, 64},
        {2, 256}
    };

    int _current_open_directory_slot = -1;
    uint8_t _countScannedSSIDs = 0;

    std::atomic<bool> _startup_mount_lock{false};
    unsigned char _active_rotate_slot = 0;

    virtual size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                                   uint8_t maxlen) = 0;
    dirEntryDetails _additional_direntry_details(fsdir_entry_t *f);

    // Platform hook for fujicore_mount_disk_image_success(): the base
    // implementation calls DISK_DEVICE::mount() with its original 5-arg
    // signature, so this stays the only call site that needs a
    // platform-specific override rather than a #ifdef BUILD_* (banned here,
    // see tests/check_no_build_ifdefs.py). rs232Fuji overrides this to pass
    // `host` through to rs232Disk::mount(), which MediaTypeROM needs to open
    // a same-named .cfg sibling through the same fujiHost the ROM itself
    // came from -- see lib/media/rs232/diskTypeROM.cpp.
    virtual mediatype_t mount_media(DISK_DEVICE *disk_dev, fujiDisk &disk, fujiHost &host,
                                    disk_access_flags_t mode)
    {
        return disk_dev->mount(disk.fileh, disk.filename, disk.disk_size, mode);
    }

    // Slot holding the disk that currently answers as drive 1, or -1 when
    // fewer than two disks are mounted and rotation is a no-op.
    int get_rotate_slot();

    // Platform hook for fujicmd_image_rotate(): announce the newly selected
    // drive 1. Atari speaks it through SAM; everyone else stays quiet.
    //
    virtual void announce_rotation(int drive_slot) {}

    // ============ Validation of inputs ============
    success_is_true validate_host_slot(uint8_t slot, const char *dmsg=nullptr);
    success_is_true validate_device_slot(uint8_t slot, const char *dmsg = nullptr);

    virtual void fujidev_set_device_fullpath(const FUJI_COMMAND_PACKET &packet) {
        fujicmd_set_device_filename_success(packet.param(0), packet.param(1),
                                            (disk_access_flags_t) ((uint8_t)
                                                                   packet.param(2)));
    }

public:
    bool boot_config = true;

    fujiDevice(unsigned int numDisk, std::string extension,
               std::optional<std::string> lobbyURL);
    virtual void setup() = 0;
    void shutdown() override;

    // Return true if command was handled here
    bool processCommand(const FUJI_COMMAND_PACKET &packet) override;
    // Return true if command is one that can be handled
    bool recognizesCommand(fujiCommandID_t command);

    fujiHost *get_host(int i) { return &_fnHosts[i]; }
    std::string get_host_prefix(int host_slot) { return _fnHosts[host_slot].get_prefix(); }

    fujiDisk *get_disk(int i) { return &_fnDisks[i]; }
    virtual DISK_DEVICE *get_disk_dev(int i) { return &_fnDisks[i].disk_dev; }
    fujiDeviceID_t get_disk_id(int drive_slot) {
        return SYSTEM_BUS.fujiIDForDevice(&_fnDisks[drive_slot].disk_dev);
    }

    void populate_slots_from_config();
    void populate_config_from_slots();
    fujiHost *set_slot_hostname(int host_slot, char *hostname);

    // ============ Standard Fuji commands ============
    // fujicmd_ methods are the public entry points for handling
    // commands received over the bus. Intended for ease of calling by
    // process() because they validate inputs, call the matching
    // fujicore_ logic, and send the result back via transaction_*.
    virtual success_is_true fujicmd_mount_all_success();
    virtual void fujicmd_reset();
    success_is_true fujicmd_mount_host_success(uint8_t hostSlot);
    virtual void fujicmd_net_scan_networks();
    void fujicmd_net_scan_result(uint8_t index);
    void fujicmd_net_get_ssid();
    success_is_true fujicmd_net_set_ssid_success();
    void fujicmd_net_get_wifi_enabled();
    virtual success_is_true fujicmd_mount_disk_image_success(uint8_t deviceSlot, disk_access_flags_t access_mode);
    success_is_true fujicmd_unmount_disk_image_success(uint8_t deviceSlot);
    void fujicmd_image_rotate();
    success_is_true fujicmd_open_directory_success(uint8_t hostSlot);
    virtual void fujicmd_close_directory();
    virtual void fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl);
    void fujicmd_get_directory_position();
    void fujicmd_set_directory_position(uint16_t pos);
    success_is_true fujicore_copy_file_success(uint8_t sourceSlot, uint8_t destSlot, std::string copySpec);
    success_is_true fujicmd_copy_file_success(uint8_t sourceSlot, uint8_t destSlot, std::string copySpec);
    virtual void fujicmd_get_adapter_config();
    virtual void fujicmd_get_adapter_config_extended();
    void fujicmd_get_device_filename(uint8_t slot);
    virtual success_is_true fujicmd_set_device_filename_success(uint8_t deviceSlot, uint8_t host,
                                                                disk_access_flags_t mode);
    void fujicmd_get_host_prefix(uint8_t hostSlot);
    void fujicmd_net_get_wifi_status();
    void fujicmd_read_host_slots();
    void fujicmd_write_host_slots();
    void fujicmd_set_boot_config(bool enable);
    void fujicmd_set_boot_mode(uint8_t bootMode, mediatype_t disk_type, DISK_DEVICE *disk_dev);
    void fujicmd_set_host_prefix(uint8_t hostSlot, const char *prefix=nullptr);
    success_is_true fujicmd_unmount_host_success(uint8_t hostSlot);
    void fujicmd_read_device_slots();
    void fujicmd_write_device_slots();
    void fujicmd_status();

    void fujicmd_generate_guid();
    void fujicmd_random();

    // ============ Implementations by fujicmd_ methods ============
    // These are safe to call directly if the bus abstraction
    // (transaction_) doesn't suit the platform.
    SSIDInfo fujicore_net_scan_result(uint8_t index, bool *err=nullptr);
    SSIDConfig fujicore_net_get_ssid();
    uint8_t fujicore_net_get_wifi_status();
    uint8_t fujicore_net_get_wifi_enabled();
    success_is_true fujicore_open_directory_success(uint8_t hostSlot, const std::string &dirpath);
    success_is_true fujicore_open_directory_success(uint8_t hostSlot, const std::string &dirpath,
                                                    const std::optional<std::string> &pattern);
    std::optional<std::string> fujicore_read_directory_entry(size_t maxlen, uint8_t addtl);
    uint16_t fujicore_get_directory_position();
    AdapterConfigExtended fujicore_get_adapter_config_extended();
    success_is_true fujicore_set_device_filename_success(uint8_t deviceSlot, uint8_t host,
                                                         disk_access_flags_t mode, std::string filename);
    std::optional<std::string> fujicore_get_device_filename(uint8_t slot);
    virtual success_is_true fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                              disk_access_flags_t access_mode);
    success_is_true fujicore_unmount_disk_image_success(uint8_t deviceSlot);
    success_is_true fujicore_mount_host_success(uint8_t hostSlot);
    success_is_true fujicore_mount_all_success();
    success_is_true fujicore_mount_all_at_startup();
    void fujicore_net_scan_networks();
    success_is_true fujicore_net_set_ssid_success(const char *ssid, const char *password, bool save);

    // Should be protected but being called by drivewire.cpp
    virtual void insert_boot_device(uint8_t image_id, mediatype_t disk_type, DISK_DEVICE *disk_dev);
    void insert_boot_device(std::string boot_img, mediatype_t disk_type, DISK_DEVICE *disk_dev);
};

extern fujiDevice *theFuji;

#endif // FUJIDEVICE_H
