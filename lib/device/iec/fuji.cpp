#ifdef BUILD_IEC

#include "fuji.h"

#include <driver/ledc.h>

#include <cstdint>
#include <cstring>

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnFsSPIFFS.h"
#include "fnWiFi.h"

#include "led.h"
#include "utils.h"

iecFuji theFuji; // global fuji device object

//iecNetwork sioNetDevs[MAX_NETWORK_DEVICES];

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
iecFuji::iecFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void iecFuji::status()
{
    // TODO IMPLEMENT
}

// Reset FujiNet
void iecFuji::reset_fujinet()
{
    // TODO IMPLEMENT
}

// Scan for networks
void iecFuji::net_scan_networks()
{
    // TODO IMPLEMENT
}

// Return scanned network entry
void iecFuji::net_scan_result()
{
    // TODO IMPLEMENT
}

//  Get SSID
void iecFuji::net_get_ssid()
{
    // TODO IMPLEMENT    
}

// Set SSID
void iecFuji::net_set_ssid()
{
    // TODO IMPLEMENT
}

// Get WiFi Status
void iecFuji::net_get_wifi_status()
{
    // TODO IMPLEMENT
}

// Check if Wifi is enabled
void iecFuji::net_get_wifi_enabled()
{
    // TODO IMPLEMENT
}

// Mount Server
void iecFuji::mount_host()
{
    // TODO IMPLEMENT
}

// Disk Image Mount
void iecFuji::disk_image_mount()
{
    // TODO IMPLEMENT
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void iecFuji::set_boot_config()
{
    // TODO IMPLEMENT
}

// Do SIO copy
void iecFuji::copy_file()
{
    // TODO IMPLEMENT
}

// Mount all
void iecFuji::mount_all()
{
    // TODO IMPLEMENT
}

// Set boot mode
void iecFuji::set_boot_mode()
{
    // TODO IMPLEMENT
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
void iecFuji::open_app_key()
{
    // TODO IMPLEMENT
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void iecFuji::close_app_key()
{
    // TODO IMPLEMENT
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void iecFuji::write_app_key()
{
    // TODO IMPLEMENT
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void iecFuji::read_app_key()
{
    // TODO IMPLEMENT
}

// Disk Image Unmount
void iecFuji::disk_image_umount()
{
    // TODO IMPLEMENT
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void iecFuji::image_rotate()
{
    // TODO IMPLEMENT
}

// This gets called when we're about to shutdown/reboot
void iecFuji::shutdown()
{
    // TODO IMPLEMENT
}

void iecFuji::open_directory()
{
    // TODO IMPLEMENT
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    // TODO IMPLEMENT
}

void iecFuji::read_directory_entry()
{
    // TODO IMPLEMENT
}

void iecFuji::get_directory_position()
{
    // TODO IMPLEMENT
}

void iecFuji::set_directory_position()
{
    // TODO IMPLEMENT
}

void iecFuji::close_directory()
{
    // TODO IMPLEMENT
}

// Get network adapter configuration
void iecFuji::get_adapter_config()
{
    // TODO IMPLEMENT
}

//  Make new disk and shove into device slot
void iecFuji::new_disk()
{
    // TODO IMPLEMENT
}

// Send host slot data to computer
void iecFuji::read_host_slots()
{
    // TODO IMPLEMENT
}

// Read and save host slot data from computer
void iecFuji::write_host_slots()
{
    // TODO IMPLEMENT
}

// Store host path prefix
void iecFuji::set_host_prefix()
{
    // TODO IMPLEMENT
}

// Retrieve host path prefix
void iecFuji::get_host_prefix()
{
    // TODO IMPLEMENT
}

// Send device slot data to computer
void iecFuji::read_device_slots()
{
    // TODO IMPLEMENT
}

// Read and save disk slot data from computer
void iecFuji::write_device_slots()
{
    // TODO IMPLEMENT
}

// Temporary(?) function while we move from old config storage to new
void iecFuji::_populate_slots_from_config()
{
    // TODO IMPLEMENT
}

// Temporary(?) function while we move from old config storage to new
void iecFuji::_populate_config_from_slots()
{
    // TODO IMPLEMENT
}

// Write a 256 byte filename to the device slot
void iecFuji::set_device_filename()
{
    // TODO IMPLEMENT
}

// Get a 256 byte filename from device slot
void iecFuji::get_device_filename()
{
    // TODO IMPLEMENT
}

// Mounts the desired boot disk number
void iecFuji::insert_boot_device(uint8_t d)
{
    // TODO IMPLEMENT
}

// Initializes base settings and adds our devices to the SIO bus
void iecFuji::setup(systemBus *siobus)
{
    // TODO IMPLEMENT
    Debug_printf("iecFuji::setup()\n");
    IEC.addDevice(this,0x0F);
}

iecDisk *iecFuji::bootdisk()
{
    return &_bootDisk;
}

void iecFuji::tin()
{
    Debug_printf("tin()\n");
    if (commanddata->secondary == IEC_REOPEN)
        IEC.sendBytes("TESTING FROM OPEN\n");

}

void iecFuji::tout()
{
}

device_state_t iecFuji::process(IECData *id)
{
    virtualDevice::process(id);

    dumpData();

    if (commanddata->channel != 15)
    {
        Debug_printf("Fuji device only accepts on channel 15. Sending NOTFOUND.\n");
        device_state=DEVICE_ERROR;
        IEC.senderTimeout();
    }

    if (payload == "TIN")
        tin();
    else if (payload == "TOUT")
        tout();

    return device_state;
}

int iecFuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string iecFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

#endif /* BUILD_IEC */