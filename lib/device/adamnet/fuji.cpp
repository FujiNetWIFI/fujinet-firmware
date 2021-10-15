#ifdef BUILD_ADAM

#include <cstdint>
#include <driver/ledc.h>

#include "fuji.h"
#include "led.h"
#include "fnWiFi.h"
#include "fnSystem.h"

#include "../utils/utils.h"
#include "../FileSystem/fnFsSPIF.h"
#include "../config/fnConfig.h"

#define SIO_FUJICMD_RESET 0xFF
#define SIO_FUJICMD_GET_SSID 0xFE
#define SIO_FUJICMD_SCAN_NETWORKS 0xFD
#define SIO_FUJICMD_GET_SCAN_RESULT 0xFC
#define SIO_FUJICMD_SET_SSID 0xFB
#define SIO_FUJICMD_GET_WIFISTATUS 0xFA
#define SIO_FUJICMD_MOUNT_HOST 0xF9
#define SIO_FUJICMD_MOUNT_IMAGE 0xF8
#define SIO_FUJICMD_OPEN_DIRECTORY 0xF7
#define SIO_FUJICMD_READ_DIR_ENTRY 0xF6
#define SIO_FUJICMD_CLOSE_DIRECTORY 0xF5
#define SIO_FUJICMD_READ_HOST_SLOTS 0xF4
#define SIO_FUJICMD_WRITE_HOST_SLOTS 0xF3
#define SIO_FUJICMD_READ_DEVICE_SLOTS 0xF2
#define SIO_FUJICMD_WRITE_DEVICE_SLOTS 0xF1
#define SIO_FUJICMD_UNMOUNT_IMAGE 0xE9
#define SIO_FUJICMD_GET_ADAPTERCONFIG 0xE8
#define SIO_FUJICMD_NEW_DISK 0xE7
#define SIO_FUJICMD_UNMOUNT_HOST 0xE6
#define SIO_FUJICMD_GET_DIRECTORY_POSITION 0xE5
#define SIO_FUJICMD_SET_DIRECTORY_POSITION 0xE4
#define SIO_FUJICMD_SET_HSIO_INDEX 0xE3
#define SIO_FUJICMD_SET_DEVICE_FULLPATH 0xE2
#define SIO_FUJICMD_SET_HOST_PREFIX 0xE1
#define SIO_FUJICMD_GET_HOST_PREFIX 0xE0
#define SIO_FUJICMD_SET_SIO_EXTERNAL_CLOCK 0xDF
#define SIO_FUJICMD_WRITE_APPKEY 0xDE
#define SIO_FUJICMD_READ_APPKEY 0xDD
#define SIO_FUJICMD_OPEN_APPKEY 0xDC
#define SIO_FUJICMD_CLOSE_APPKEY 0xDB
#define SIO_FUJICMD_GET_DEVICE_FULLPATH 0xDA
#define SIO_FUJICMD_CONFIG_BOOT 0xD9
#define SIO_FUJICMD_COPY_FILE 0xD8
#define SIO_FUJICMD_MOUNT_ALL 0xD7
#define SIO_FUJICMD_SET_BOOT_MODE 0xD6
#define SIO_FUJICMD_STATUS 0x53
#define SIO_FUJICMD_HSIO_INDEX 0x3F

adamFuji theFuji; // global fuji device object

//sioDisk sioDiskDevs[MAX_HOSTS];
//sioNetwork sioNetDevs[MAX_NETWORK_DEVICES];

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

/**
 * Say the numbers 1-8 using phonetic tweaks.
 * @param n The number to say.
 */
void say_number(unsigned char n)
{
    switch (n)
    {
    case 1:
        util_sam_say("WAH7NQ", true);
        break;
    case 2:
        util_sam_say("TUW7", true);
        break;
    case 3:
        util_sam_say("THRIYY7Q", true);
        break;
    case 4:
        util_sam_say("FOH7R", true);
        break;
    case 5:
        util_sam_say("F7AYVQ", true);
        break;
    case 6:
        util_sam_say("SIH7IHKSQ", true);
        break;
    case 7:
        util_sam_say("SEHV7EHNQ", true);
        break;
    case 8:
        util_sam_say("AEY74Q", true);
        break;
    default:
        Debug_printf("say_number() - Uncaught number %d", n);
    }
}

/**
 * Say swap label
 */
void say_swap_label()
{
    // DISK
    util_sam_say("DIHSK7Q ", true);
}

// Constructor
adamFuji::adamFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void adamFuji::adamnet_control_status()
{

}

// Reset FujiNet
void adamFuji::adamnet_reset_fujinet()
{

}

// Scan for networks
void adamFuji::adamnet_net_scan_networks()
{

}

// Return scanned network entry
void adamFuji::adamnet_net_scan_result()
{

}

//  Get SSID
void adamFuji::adamnet_net_get_ssid()
{

}

// Set SSID
void adamFuji::adamnet_net_set_ssid()
{

}

// Get WiFi Status
void adamFuji::adamnet_net_get_wifi_status()
{

}

// Mount Server
void adamFuji::adamnet_mount_host()
{

}

// Disk Image Mount
void adamFuji::adamnet_disk_image_mount()
{

}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void adamFuji::adamnet_set_boot_config()
{

}

// Do SIO copy
void adamFuji::adamnet_copy_file()
{

}

// Mount all
void adamFuji::adamnet_mount_all()
{

}

// Set boot mode
void adamFuji::adamnet_set_boot_mode()
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
void adamFuji::adamnet_open_app_key()
{

}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void adamFuji::adamnet_close_app_key()
{

}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void adamFuji::adamnet_write_app_key()
{

}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void adamFuji::adamnet_read_app_key()
{

}

// DEBUG TAPE
void adamFuji::debug_tape()
{

}

// Disk Image Unmount
void adamFuji::adamnet_disk_image_umount()
{

}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void adamFuji::image_rotate()
{
    Debug_println("Fuji cmd: IMAGE ROTATE");

    int count = 0;
    // Find the first empty slot
    while (_fnDisks[count].fileh != nullptr)
        count++;

    if (count > 1)
    {
        count--;

        // Save the device ID of the disk in the last slot
        int last_id = _fnDisks[count].disk_dev.id();

        for (int n = count; n > 0; n--)
        {
            int swap = _fnDisks[n - 1].disk_dev.id();
            Debug_printf("setting slot %d to ID %hx\n", n, swap);
            _adamnet_bus->changeDeviceId(&_fnDisks[n].disk_dev, swap);
        }

        // The first slot gets the device ID of the last slot
        _adamnet_bus->changeDeviceId(&_fnDisks[0].disk_dev, last_id);

        // Say whatever disk is in D1:
        if (Config.get_general_rotation_sounds())
        {
            for (int i = 0; i <= count; i++)
            {
                if (_fnDisks[i].disk_dev.id() == 0x31)
                {
                    say_swap_label();
                    say_number(i + 1); // because i starts from 0
                }
            }
        }
    }
}

// This gets called when we're about to shutdown/reboot
void adamFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

void adamFuji::adamnet_open_directory()
{

}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);
    modtime->tm_mon++;
    modtime->tm_year -= 70;

    dest[0] = modtime->tm_year;
    dest[1] = modtime->tm_mon;
    dest[2] = modtime->tm_mday;
    dest[3] = modtime->tm_hour;
    dest[4] = modtime->tm_min;
    dest[5] = modtime->tm_sec;

    // File size
    uint16_t fsize = f->size;
    dest[6] = LOBYTE_FROM_UINT16(fsize);
    dest[7] = HIBYTE_FROM_UINT16(fsize);

    // File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

    dest[8] = f->isDir ? FF_DIR : 0;

    maxlen -= 10; // Adjust the max return value with the number of additional bytes we're copying
    if (f->isDir) // Also subtract a byte for a terminating slash on directories
        maxlen--;
    if (strlen(f->filename) >= maxlen)
        dest[8] |= FF_TRUNC;

    // File type
    dest[9] = MediaType::discover_mediatype(f->filename);
}

void adamFuji::adamnet_read_directory_entry()
{

}

void adamFuji::adamnet_get_directory_position()
{

}

void adamFuji::adamnet_set_directory_position()
{

}

void adamFuji::adamnet_close_directory()
{

}

// Get network adapter configuration
void adamFuji::adamnet_get_adapter_config()
{

}

//  Make new disk and shove into device slot
void adamFuji::adamnet_new_disk()
{

}

// Send host slot data to computer
void adamFuji::adamnet_read_host_slots()
{

}

// Read and save host slot data from computer
void adamFuji::adamnet_write_host_slots()
{

}

// Store host path prefix
void adamFuji::adamnet_set_host_prefix()
{

}

// Retrieve host path prefix
void adamFuji::adamnet_get_host_prefix()
{

}

// Send device slot data to computer
void adamFuji::adamnet_read_device_slots()
{

}

// Read and save disk slot data from computer
void adamFuji::adamnet_write_device_slots()
{

}

// Temporary(?) function while we move from old config storage to new
void adamFuji::_populate_slots_from_config()
{
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        if (Config.get_host_type(i) == fnConfig::host_types::HOSTTYPE_INVALID)
            _fnHosts[i].set_hostname("");
        else
            _fnHosts[i].set_hostname(Config.get_host_name(i).c_str());
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        _fnDisks[i].reset();

        if (Config.get_mount_host_slot(i) != HOST_SLOT_INVALID)
        {
            if (Config.get_mount_host_slot(i) >= 0 && Config.get_mount_host_slot(i) <= MAX_HOSTS)
            {
                strlcpy(_fnDisks[i].filename,
                        Config.get_mount_path(i).c_str(), sizeof(fujiDisk::filename));
                _fnDisks[i].host_slot = Config.get_mount_host_slot(i);
                if (Config.get_mount_mode(i) == fnConfig::mount_modes::MOUNTMODE_WRITE)
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_WRITE;
                else
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_READ;
            }
        }
    }
}

// Temporary(?) function while we move from old config storage to new
void adamFuji::_populate_config_from_slots()
{
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        fujiHostType htype = _fnHosts[i].get_type();
        const char *hname = _fnHosts[i].get_hostname();

        if (hname[0] == '\0')
        {
            Config.clear_host(i);
        }
        else
        {
            Config.store_host(i, hname,
                              htype == HOSTTYPE_TNFS ? fnConfig::host_types::HOSTTYPE_TNFS : fnConfig::host_types::HOSTTYPE_SD);
        }
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (_fnDisks[i].host_slot >= MAX_HOSTS || _fnDisks[i].filename[0] == '\0')
            Config.clear_mount(i);
        else
            Config.store_mount(i, _fnDisks[i].host_slot, _fnDisks[i].filename,
                               _fnDisks[i].access_mode == DISK_ACCESS_MODE_WRITE ? fnConfig::mount_modes::MOUNTMODE_WRITE : fnConfig::mount_modes::MOUNTMODE_READ);
    }
}

// Write a 256 byte filename to the device slot
void adamFuji::adamnet_set_device_filename()
{
    
}

// Get a 256 byte filename from device slot
void adamFuji::adamnet_get_device_filename()
{

}

// Mounts the desired boot disk number
void adamFuji::insert_boot_device(uint8_t d)
{
    const char *config_atr = "/autorun.ddp";
    const char *mount_all_atr = "/mount-and-boot.ddp";
    FILE *fBoot;

    _bootDisk.unmount();

    switch (d)
    {
    case 0:
        fBoot = fnSPIFFS.file_open(config_atr);
        _bootDisk.mount(fBoot, config_atr, 0);
        break;
    case 1:
        fBoot = fnSPIFFS.file_open(mount_all_atr);
        _bootDisk.mount(fBoot, mount_all_atr, 0);
        break;
    }

    _bootDisk.is_config_device = true;
    _bootDisk.device_active = false;
}

// Initializes base settings and adds our devices to the SIO bus
void adamFuji::setup(adamNetBus *siobus)
{
    // set up Fuji device
    _adamnet_bus = siobus;

    _populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode());

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();
    
    //Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();

    // Add our devices to the SIO bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _adamnet_bus->addDevice(&_fnDisks[i].disk_dev, ADAMNET_DEVICEID_DISK + i);

    // for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
    //     _adamnet_bus->addDevice(&sioNetDevs[i], ADAMNET_DEVICEID_FN_NETWORK + i);

}

adamDisk *adamFuji::bootdisk()
{
    return &_bootDisk;
}

void adamFuji::adamnet_process(uint8_t b)
{

}

int adamFuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string adamFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

#endif /* BUILD_ADAM */