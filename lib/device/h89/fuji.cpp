#ifdef BUILD_H89

#include "fuji.h"

#include <cstring>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"

#include "utils.h"

#define ADDITIONAL_DETAILS_BYTES 12

H89Fuji theFuji;        // global fuji device object
H89Network *theNetwork; // global network device object (temporary)
H89Printer *thePrinter; // global printer

// sioDisk sioDiskDevs[MAX_HOSTS];
// sioNetwork sioNetDevs[MAX_NETWORK_DEVICES];

bool _validate_host_slot(uint8_t slot, const char *dmsg = nullptr);
bool _validate_device_slot(uint8_t slot, const char *dmsg = nullptr);

bool _validate_host_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_HOSTS)
        return true;

    if (dmsg == NULL)
    {
        Debug_printf("!! Invalid host slot %hu\n", slot);
    }
    else
    {
        Debug_printf("!! Invalid host slot %hu @ %s\n", slot, dmsg);
    }

    return false;
}

bool _validate_device_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_DISK_DEVICES)
        return true;

    if (dmsg == NULL)
    {
        Debug_printf("!! Invalid device slot %hu\n", slot);
    }
    else
    {
        Debug_printf("!! Invalid device slot %hu @ %s\n", slot, dmsg);
    }

    return false;
}

// Constructor
H89Fuji::H89Fuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Reset FujiNet
void H89Fuji::H89_reset_fujinet()
{
}

// Scan for networks
void H89Fuji::H89_net_scan_networks()
{
}

// Return scanned network entry
void H89Fuji::H89_net_scan_result()
{
}

//  Get SSID
void H89Fuji::H89_net_get_ssid()
{
}

// Set SSID
void H89Fuji::H89_net_set_ssid()
{
}

// Get WiFi Status
void H89Fuji::H89_net_get_wifi_status()
{
}

// Mount Server
void H89Fuji::H89_mount_host()
{
}

// Disk Image Mount
void H89Fuji::H89_disk_image_mount()
{
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void H89Fuji::H89_set_boot_config()
{
}

// Do SIO copy
void H89Fuji::H89_copy_file()
{
}

// Set boot mode
void H89Fuji::H89_set_boot_mode()
{
}

char *_generate_appkey_filename(appkey *info)
{
    static char filenamebuf[30];

    snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key", info->creator, info->app, info->key);
    return filenamebuf;
}

/*
 Opens an "app key".  This just sets the needed app key parameters (creator, app, key, mode)
 for the subsequent expected read/write command. We could've added this information as part
 of the payload in a WRITE_APPKEY command, but there was no way to do this for READ_APPKEY.
 Requiring a separate OPEN command makes both the read and write commands behave similarly
 and leaves the possibity for a more robust/general file read/write function later.
*/
void H89Fuji::H89_open_app_key()
{
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void H89Fuji::H89_close_app_key()
{
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void H89Fuji::H89_write_app_key()
{
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void H89Fuji::H89_read_app_key()
{
}

// DEBUG TAPE
void H89Fuji::debug_tape()
{
}

// Disk Image Unmount
void H89Fuji::H89_disk_image_umount()
{
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void H89Fuji::image_rotate()
{
}

// This gets called when we're about to shutdown/reboot
void H89Fuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

char dirpath[256];

void H89Fuji::H89_open_directory()
{
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
}

void H89Fuji::H89_read_directory_entry()
{
}

void H89Fuji::H89_get_directory_position()
{
}

void H89Fuji::H89_set_directory_position()
{
}

void H89Fuji::H89_close_directory()
{
}

// Get network adapter configuration
void H89Fuji::H89_get_adapter_config()
{
}

//  Make new disk and shove into device slot
void H89Fuji::H89_new_disk()
{
}

// Send host slot data to computer
void H89Fuji::H89_read_host_slots()
{
}

// Read and save host slot data from computer
void H89Fuji::H89_write_host_slots()
{
}

// Store host path prefix
void H89Fuji::H89_set_host_prefix()
{
}

// Retrieve host path prefix
void H89Fuji::H89_get_host_prefix()
{
}

// Public method to update host in specific slot
fujiHost *H89Fuji::set_slot_hostname(int host_slot, char *hostname)
{
    _fnHosts[host_slot].set_hostname(hostname);
    _populate_config_from_slots();
    return &_fnHosts[host_slot];
}

// Send device slot data to computer
void H89Fuji::H89_read_device_slots()
{
}

// Read and save disk slot data from computer
void H89Fuji::H89_write_device_slots()
{
}

// Temporary(?) function while we move from old config storage to new
void H89Fuji::_populate_slots_from_config()
{
}

// Temporary(?) function while we move from old config storage to new
void H89Fuji::_populate_config_from_slots()
{
}

char f[MAX_FILENAME_LEN];

// Write a 256 byte filename to the device slot
void H89Fuji::H89_set_device_filename()
{
}

// Get a 256 byte filename from device slot
void H89Fuji::H89_get_device_filename()
{
}


void H89Fuji::H89_enable_device()
{
}

void H89Fuji::H89_disable_device()
{
}

void H89Fuji::H89_device_enabled_status()
{
}

// Initializes base settings and adds our devices to the SIO bus
void H89Fuji::setup()
{
    _populate_slots_from_config();

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = false;

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = false;
}

// Mount all
void H89Fuji::mount_all()
{
}

H89Disk *H89Fuji::bootdisk()
{
    return _bootDisk;
}


void H89Fuji::process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    // case FUJICMD_STATUS:
    //     H89_response_ack();
    //     break;
    case FUJICMD_RESET:
        H89_reset_fujinet();
        break;
    case FUJICMD_SCAN_NETWORKS:
        H89_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        H89_net_scan_result();
        break;
    case FUJICMD_SET_SSID:
        H89_net_set_ssid();
        break;
    case FUJICMD_GET_SSID:
        H89_net_get_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        H89_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        H89_mount_host();
        break;
    case FUJICMD_MOUNT_IMAGE:
        H89_disk_image_mount();
        break;
    case FUJICMD_OPEN_DIRECTORY:
        H89_open_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        H89_read_directory_entry();
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        H89_close_directory();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        H89_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        H89_set_directory_position();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        H89_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        H89_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        H89_read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        H89_write_device_slots();
        break;
    //case FUJICMD_GET_WIFI_ENABLED:
    //    H89_net_get_wifi_enabled();
    //    break;
    case FUJICMD_UNMOUNT_IMAGE:
        H89_disk_image_umount();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        H89_get_adapter_config();
        break;
    // case FUJICMD_NEW_DISK:
    //     rs232_ack();
    //     rs232_new_disk();
    //     break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        H89_set_device_filename();
        break;
    // case FUJICMD_SET_HOST_PREFIX:
    //     H89_set_host_prefix();
    //     break;
    // case FUJICMD_GET_HOST_PREFIX:
    //     H89_get_host_prefix();
    //     break;
    // case FUJICMD_WRITE_APPKEY:
    //     H89_write_app_key();
    //     break;
    // case FUJICMD_READ_APPKEY:
    //     H89_read_app_key();
    //     break;
    // case FUJICMD_OPEN_APPKEY:
    //     H89_open_app_key();
    //     break;
    // case FUJICMD_CLOSE_APPKEY:
    //     H89_close_app_key();
    //     break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        H89_get_device_filename();
        break;
    case FUJICMD_CONFIG_BOOT:
        H89_set_boot_config();
        break;
    // case FUJICMD_COPY_FILE:
    //     rs232_ack();
    //     rs232_copy_file();
    //     break;
    case FUJICMD_MOUNT_ALL:
        mount_all();
        break;
    // case FUJICMD_SET_BOOT_MODE:
    //     rs232_ack();
    //     rs232_set_boot_mode();
    //     break;
    // case FUJICMD_ENABLE_UDPSTREAM:
    //     rs232_ack();
    //     rs232_enable_udpstream();
    //     break;
    case FUJICMD_ENABLE_DEVICE:
        H89_enable_device();
        break;
    case FUJICMD_DISABLE_DEVICE:
        H89_disable_device();
        break;
    // case FUJICMD_RANDOM_NUMBER:
        // H89_random_number();
        // break;
    // case FUJICMD_GET_TIME:
        // H89_get_time();
        // break;
    case FUJICMD_DEVICE_ENABLE_STATUS:
        H89_device_enabled_status();
        break;
    default:
        fnUartDebug.printf("H89_process() not implemented yet for this device. Cmd received: %02x\n", cmdFrame.comnd);
    }
}

int H89Fuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string H89Fuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

#endif /* NEW_TARGET */
