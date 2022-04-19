#ifdef BUILD_CBM

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


#define BUS_FUJICMD_RESET 0xFF
#define BUS_FUJICMD_GET_SSID 0xFE
#define BUS_FUJICMD_SCAN_NETWORKS 0xFD
#define BUS_FUJICMD_GET_SCAN_RESULT 0xFC
#define BUS_FUJICMD_SET_SSID 0xFB
#define BUS_FUJICMD_GET_WIFISTATUS 0xFA
#define BUS_FUJICMD_MOUNT_HOST 0xF9
#define BUS_FUJICMD_MOUNT_IMAGE 0xF8
#define BUS_FUJICMD_OPEN_DIRECTORY 0xF7
#define BUS_FUJICMD_READ_DIR_ENTRY 0xF6
#define BUS_FUJICMD_CLOSE_DIRECTORY 0xF5
#define BUS_FUJICMD_READ_HOST_SLOTS 0xF4
#define BUS_FUJICMD_WRITE_HOST_SLOTS 0xF3
#define BUS_FUJICMD_READ_DEVICE_SLOTS 0xF2
#define BUS_FUJICMD_WRITE_DEVICE_SLOTS 0xF1
#define BUS_FUJICMD_UNMOUNT_IMAGE 0xE9
#define BUS_FUJICMD_GET_ADAPTERCONFIG 0xE8
#define BUS_FUJICMD_NEW_DISK 0xE7
#define BUS_FUJICMD_UNMOUNT_HOST 0xE6
#define BUS_FUJICMD_GET_DIRECTORY_POSITION 0xE5
#define BUS_FUJICMD_SET_DIRECTORY_POSITION 0xE4
#define BUS_FUJICMD_SET_HSIO_INDEX 0xE3
#define BUS_FUJICMD_SET_DEVICE_FULLPATH 0xE2
#define BUS_FUJICMD_SET_HOST_PREFIX 0xE1
#define BUS_FUJICMD_GET_HOST_PREFIX 0xE0
#define BUS_FUJICMD_SET_SIO_EXTERNAL_CLOCK 0xDF
#define BUS_FUJICMD_WRITE_APPKEY 0xDE
#define BUS_FUJICMD_READ_APPKEY 0xDD
#define BUS_FUJICMD_OPEN_APPKEY 0xDC
#define BUS_FUJICMD_CLOSE_APPKEY 0xDB
#define BUS_FUJICMD_GET_DEVICE_FULLPATH 0xDA
#define BUS_FUJICMD_CONFIG_BOOT 0xD9
#define BUS_FUJICMD_COPY_FILE 0xD8
#define BUS_FUJICMD_MOUNT_ALL 0xD7
#define BUS_FUJICMD_SET_BOOT_MODE 0xD6
#define BUS_FUJICMD_STATUS 0x53
#define BUS_FUJICMD_HSIO_INDEX 0x3F

iecFuji theFuji; // global fuji device object

//iecDisk iecDiskDevs[MAX_HOSTS];
iecNetwork iecNetDevs[MAX_NETWORK_DEVICES];

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
fujiDevice::fujiDevice()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void fujiDevice::bus_status()
{
    Debug_println("Fuji cmd: STATUS");

    char ret[4] = {0};

    bus_to_computer((uint8_t *)ret, sizeof(ret), false);
    return;
}

// Reset FujiNet
void fujiDevice::bus_reset_fujinet()
{
    Debug_println("Fuji cmd: REBOOT");
    bus_complete();
    fnSystem.reboot();
}

// Scan for networks
void fujiDevice::bus_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    char ret[4] = {0};

    _countScannedSSIDs = fnWiFi.scan_networks();

    ret[0] = _countScannedSSIDs;

    bus_to_computer((uint8_t *)ret, 4, false);
}

// Return scanned network entry
void fujiDevice::bus_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");

    // Response to BUS_FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        uint8_t rssi;
    } detail;

    bool err = false;
    if (cmdFrame.aux1 < _countScannedSSIDs)
        fnWiFi.get_scan_result(cmdFrame.aux1, detail.ssid, &detail.rssi);
    else
    {
        memset(&detail, 0, sizeof(detail));
        err = true;
    }

    bus_to_computer((uint8_t *)&detail, sizeof(detail), err);
}

//  Get SSID
void fujiDevice::bus_net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    // Response to BUS_FUJICMD_GET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    /*
     We memcpy instead of strcpy because technically the SSID and phasephras aren't strings and aren't null terminated,
     they're arrays of bytes officially and can contain any byte value - including a zero - at any point in the array.
     However, we're not consistent about how we treat this in the different parts of the code.
    */
    std::string s = Config.get_wifi_ssid();
    memcpy(cfg.ssid, s.c_str(),
           s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(cfg.password, s.c_str(),
           s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

    bus_to_computer((uint8_t *)&cfg, sizeof(cfg), false);
}

// Set SSID
void fujiDevice::bus_net_set_ssid()
{
    Debug_println("Fuji cmd: SET SSID");

    // Data for BUS_FUJICMD_SET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    uint8_t ck = bus_to_peripheral((uint8_t *)&cfg, sizeof(cfg));

    if (bus_checksum((uint8_t *)&cfg, sizeof(cfg)) != ck)
        bus_error();
    else
    {
        bool save = cmdFrame.aux1 != 0;

        Debug_printf("Connecting to net: %s password: %s\n", cfg.ssid, cfg.password);

        fnWiFi.connect(cfg.ssid, cfg.password);

        // Only save these if we're asked to, otherwise assume it was a test for connectivity
        if (save)
        {
            Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
            Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
            Config.save();
        }

        bus_complete();
    }
}

// Get WiFi Status
void fujiDevice::bus_net_get_wifi_status()
{
    Debug_println("Fuji cmd: GET WIFI STATUS");
    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    bus_to_computer(&wifiStatus, sizeof(wifiStatus), false);
}

// Mount Server
void fujiDevice::bus_mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = cmdFrame.aux1;

    // Make sure we weren't given a bad hostSlot
    if (!_validate_host_slot(hostSlot, "bus_tnfs_mount_hosts"))
    {
        bus_error();
        return;
    }

    if (!_fnHosts[hostSlot].mount())
        bus_error();
    else
        bus_complete();
}

// Disk Image Mount
void fujiDevice::bus_disk_image_mount()
{
    // TAPE or CASSETTE handling: this function can also mount CAS and WAV files
    // to the C: device. Everything stays the same here and the mounting
    // where all the magic happens is done in the iecDisk::mount() function.
    // This function opens the file, so cassette does not need to open the file.
    // Cassette needs the file pointer and file size.

    Debug_println("Fuji cmd: MOUNT IMAGE");

    uint8_t deviceSlot = cmdFrame.aux1;
    uint8_t options = cmdFrame.aux2; // DISK_ACCESS_MODE

    // TODO: Implement FETCH?
    char flag[3] = {'r', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    // Make sure we weren't given a bad hostSlot
    if (!_validate_device_slot(deviceSlot))
    {
        bus_error();
        return;
    }
    
    if (!_validate_host_slot(_fnDisks[deviceSlot].host_slot))
    {
        bus_error();
        return;
    }
    
    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 disk.filename, disk.host_slot, flag, deviceSlot + 1);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    if (disk.fileh == nullptr)
    {
        bus_error();
        return;
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;
    status_wait_count = 0;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);

    bus_complete();
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void fujiDevice::bus_set_boot_config()
{
    boot_config = cmdFrame.aux1;
    bus_complete();
}

// Do SIO copy
void fujiDevice::bus_copy_file()
{
    uint8_t csBuf[256];
    string copySpec;
    string sourcePath;
    string destPath;
    uint8_t ck;
    FILE *sourceFile;
    FILE *destFile;
    char *dataBuf;
    unsigned char sourceSlot;
    unsigned char destSlot;

    dataBuf = (char *)malloc(532);

    if (dataBuf == nullptr)
    {
        bus_error();
        return;
    }

    memset(&csBuf, 0, sizeof(csBuf));

    ck = bus_to_peripheral(csBuf, sizeof(csBuf));

    if (ck != bus_checksum(csBuf, sizeof(csBuf)))
    {
        bus_error();
        return;
    }

    copySpec = string((char *)csBuf);

    Debug_printf("copySpec: %s\n", copySpec.c_str());

    // Check for malformed copyspec.
    if (copySpec.empty() || copySpec.find_first_of("|") == string::npos)
    {
        bus_error();
        return;
    }

    if (cmdFrame.aux1 < 1 || cmdFrame.aux1 > 8)
    {
        bus_error();
        return;
    }

    if (cmdFrame.aux2 < 1 || cmdFrame.aux2 > 8)
    {
        bus_error();
        return;
    }

    sourceSlot = cmdFrame.aux1 - 1;
    destSlot = cmdFrame.aux2 - 1;

    // All good, after this point...

    // Chop up copyspec.
    sourcePath = copySpec.substr(0, copySpec.find_first_of("|"));
    destPath = copySpec.substr(copySpec.find_first_of("|") + 1);

    // At this point, if last part of dest path is / then copy filename from source.
    if (destPath.back() == '/')
    {
        Debug_printf("append source file\n");
        string sourceFilename = sourcePath.substr(sourcePath.find_last_of("/") + 1);
        destPath += sourceFilename;
    }

    // Mount hosts, if needed.
    _fnHosts[sourceSlot].mount();
    _fnHosts[destSlot].mount();

    // Open files...
    sourceFile = _fnHosts[sourceSlot].file_open(sourcePath.c_str(), (char *)sourcePath.c_str(), sourcePath.size() + 1, "r");

    if (sourceFile == nullptr)
    {
        bus_error();
        return;
    }

    destFile = _fnHosts[destSlot].file_open(destPath.c_str(), (char *)destPath.c_str(), destPath.size() + 1, "w");

    if (destFile == nullptr)
    {
        bus_error();
        return;
    }

    size_t count = 0;
    do
    {
        count = fread(dataBuf, 1, 532, sourceFile);
        fwrite(dataBuf, 1, count, destFile);
    } while (count > 0);

    bus_complete();

    // copyEnd:
    fclose(sourceFile);
    fclose(destFile);
    free(dataBuf);
}

// Mount all
void fujiDevice::bus_mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < 8; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != 0xFF)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                bus_error();
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                bus_error();
                return;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;
            status_wait_count = 0;

            // We need the file size for loading XEX files and for CASSETTE, so get that too
            disk.disk_size = host.file_size(disk.fileh);

            // And now mount it
            disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
        }
    }

    if (nodisks){
        // No disks in a slot, disable config
        boot_config = false;
    }

    bus_complete();
}

// Set boot mode
void fujiDevice::bus_set_boot_mode()
{
    insert_boot_device(cmdFrame.aux1);
    boot_config = true;
    bus_complete();
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
void fujiDevice::bus_open_app_key()
{
    Debug_print("Fuji cmd: OPEN APPKEY\n");

    // The data expected for this command
    uint8_t ck = bus_to_peripheral((uint8_t *)&_current_appkey, sizeof(_current_appkey));

    if (bus_checksum((uint8_t *)&_current_appkey, sizeof(_current_appkey)) != ck)
    {
        bus_error();
        return;
    }

    // We're only supporting writing to SD, so return an error if there's no SD mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        bus_error();
        return;
    }

    // Basic check for valid data
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        bus_error();
        return;
    }

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, filename = \"%s\"\n",
                 _current_appkey.creator, _current_appkey.app, _current_appkey.key, _current_appkey.mode,
                 _generate_appkey_filename(&_current_appkey));

    bus_complete();
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void fujiDevice::bus_close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    bus_complete();
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void fujiDevice::bus_write_app_key()
{
    uint16_t keylen = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);

    Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\n", keylen);

    // Data for BUS_FUJICMD_WRITE_APPKEY
    uint8_t value[MAX_APPKEY_LEN];

    uint8_t ck = bus_to_peripheral((uint8_t *)value, sizeof(value));

    if (bus_checksum((uint8_t *)value, sizeof(value)) != ck)
    {
        bus_error();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        bus_error();
        return;
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        bus_error();
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    // Reset the app key data so we require calling APPKEY OPEN before another attempt
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;

    Debug_printf("Writing appkey to \"%s\"\n", filename);

    // Make sure we have a "/FujiNet" directory, since that's where we're putting these files
    fnSDFAT.create_path("/FujiNet");

    FILE *fOut = fnSDFAT.file_open(filename, "w");
    if (fOut == nullptr)
    {
        Debug_printf("Failed to open/create output file: errno=%d\n", errno);
        bus_error();
        return;
    }
    size_t count = fwrite(value, 1, keylen, fOut);
    int e = errno;

    fclose(fOut);

    if (count != keylen)
    {
        Debug_printf("Only wrote %u bytes of expected %hu, errno=%d\n", count, keylen, e);
        bus_error();
    }

    bus_complete();
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void fujiDevice::bus_read_app_key()
{

    Debug_println("Fuji cmd: READ APPKEY");

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        bus_error();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_READ)
    {
        Debug_println("Invalid app key metadata - aborting");
        bus_error();
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    Debug_printf("Reading appkey from \"%s\"\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, "r");
    if (fIn == nullptr)
    {
        Debug_printf("Failed to open input file: errno=%d\n", errno);
        bus_error();
        return;
    }

    struct
    {
        uint16_t size;
        uint8_t value[MAX_APPKEY_LEN];
    } __attribute__((packed)) response;
    memset(&response, 0, sizeof(response));

    size_t count = fread(response.value, 1, sizeof(response.value), fIn);

    fclose(fIn);
    Debug_printf("Read %d bytes from input file\n", count);

    response.size = count;

    bus_to_computer((uint8_t *)&response, sizeof(response), false);
}

// DEBUG TAPE
void fujiDevice::debug_tape()
{
    // if not mounted then disable cassette and do nothing
    // if mounted then activate cassette
    // if mounted and active, then deactivate
    // no longer need to handle file open/close
    if (_cassetteDev.is_mounted() == true)
    {
        if (_cassetteDev.is_active() == false)
        {
            Debug_println("::debug_tape ENABLE");
            _cassetteDev.bus_enable_cassette();
        }
        else
        {
            Debug_println("::debug_tape DISABLE");
            _cassetteDev.bus_disable_cassette();
        }
    }
    else
    {
        Debug_println("::debug_tape NO CAS FILE MOUNTED");
        Debug_println("::debug_tape DISABLE");
        _cassetteDev.bus_disable_cassette();
    }
}

// Disk Image Unmount
void fujiDevice::bus_disk_image_umount()
{
    uint8_t deviceSlot = cmdFrame.aux1;

    Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // Handle disk slots
    if (deviceSlot < MAX_DISK_DEVICES)
    {
        _fnDisks[deviceSlot].disk_dev.unmount();
        if (_fnDisks[deviceSlot].disk_type == MEDIATYPE_CAS || _fnDisks[deviceSlot].disk_type == MEDIATYPE_WAV)
        {
            // tell cassette it unmount
            _cassetteDev.umount_cassette_file();
            _cassetteDev.bus_disable_cassette();
        }
        _fnDisks[deviceSlot].reset();
    }
    // Handle tape
    // else if (deviceSlot == BASE_TAPE_SLOT)
    // {
    // }
    // Invalid slot
    else
    {
        bus_error();
        return;
    }

    bus_complete();
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void fujiDevice::image_rotate()
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
            _bus->changeDeviceId(&_fnDisks[n].disk_dev, swap);
        }

        // The first slot gets the device ID of the last slot
        _bus->changeDeviceId(&_fnDisks[0].disk_dev, last_id);

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
void fujiDevice::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

void fujiDevice::bus_open_directory()
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    char dirpath[256];
    uint8_t hostSlot = cmdFrame.aux1;
    uint8_t ck = bus_to_peripheral((uint8_t *)&dirpath, sizeof(dirpath));

    if (bus_checksum((uint8_t *)&dirpath, sizeof(dirpath)) != ck)
    {
        bus_error();
        return;
    }
    if (!_validate_host_slot(hostSlot))
    {
        bus_error();
        return;
    }

    // If we already have a directory open, close it first
    if (_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closign it first\n");
        _fnHosts[_current_open_directory_slot].dir_close();
        _current_open_directory_slot = -1;
    }

    // See if there's a search pattern after the directory path
    const char *pattern = nullptr;
    int pathlen = strnlen(dirpath, sizeof(dirpath));
    if (pathlen < sizeof(dirpath) - 3) // Allow for two NULLs and a 1-char pattern
    {
        pattern = dirpath + pathlen + 1;
        int patternlen = strnlen(pattern, sizeof(dirpath) - pathlen - 1);
        if (patternlen < 1)
            pattern = nullptr;
    }

    // Remove trailing slash
    if (pathlen > 1 && dirpath[pathlen - 1] == '/')
        dirpath[pathlen - 1] = '\0';

    Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath, pattern ? pattern : "");

    if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
    {
        _current_open_directory_slot = hostSlot;
        bus_complete();
    }
    else
        bus_error();
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
    dest[9] = MediaType::discover_disktype(f->filename);
}

void fujiDevice::bus_read_directory_entry()
{
    uint8_t maxlen = cmdFrame.aux1;
    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        bus_error();
        return;
    }

    char current_entry[256];

    fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();

    if (f == nullptr)
    {
        Debug_println("Reached end of of directory");
        current_entry[0] = 0x7F;
        current_entry[1] = 0x7F;
    }
    else
    {
        Debug_printf("::read_direntry \"%s\"\n", f->filename);

        int bufsize = sizeof(current_entry);
        char *filenamedest = current_entry;

#define ADDITIONAL_DETAILS_BYTES 10
        // If 0x80 is set on AUX2, send back additional information
        if (cmdFrame.aux2 & 0x80)
        {
            _set_additional_direntry_details(f, (uint8_t *)current_entry, maxlen);
            // Adjust remaining size of buffer and file path destination
            bufsize = sizeof(current_entry) - ADDITIONAL_DETAILS_BYTES;
            filenamedest = current_entry + ADDITIONAL_DETAILS_BYTES;
        }
        else
        {
            bufsize = maxlen;
        }

        //int filelen = strlcpy(filenamedest, f->filename, bufsize);
        int filelen = util_ellipsize(f->filename, filenamedest, bufsize);

        // Add a slash at the end of directory entries
        if (f->isDir && filelen < (bufsize - 2))
        {
            current_entry[filelen] = '/';
            current_entry[filelen + 1] = '\0';
        }
    }

    bus_to_computer((uint8_t *)current_entry, maxlen, false);
}

void fujiDevice::bus_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        bus_error();
        return;
    }

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        bus_error();
        return;
    }
    // Return the value we read
    bus_to_computer((uint8_t *)&pos, sizeof(pos), false);
}

void fujiDevice::bus_set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // DAUX1 and DAUX2 hold the position to seek to in low/high order
    uint16_t pos = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        bus_error();
        return;
    }

    bool result = _fnHosts[_current_open_directory_slot].dir_seek(pos);
    if (result == false)
    {
        bus_error();
        return;
    }
    bus_complete();
}

void fujiDevice::bus_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    bus_complete();
}

// Get network adapter configuration
void fujiDevice::bus_get_adapter_config()
{
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    // Response to BUS_FUJICMD_GET_ADAPTERCONFIG
    AdapterConfig cfg;

    memset(&cfg, 0, sizeof(cfg));

    strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

    if (!fnWiFi.connected())
    {
        strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
    }
    else
    {
        strlcpy(cfg.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(cfg.hostname));
        strlcpy(cfg.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(cfg.ssid));
        fnWiFi.get_current_bssid(cfg.bssid);
        fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);
        fnSystem.Net.get_ip4_dns_info(cfg.dnsIP);
    }

    fnWiFi.get_mac(cfg.macAddress);

    bus_to_computer((uint8_t *)&cfg, sizeof(cfg), false);
}

//  Make new disk and shove into device slot
void fujiDevice::bus_new_disk()
{
    Debug_println("Fuji cmd: NEW DISK");

    struct
    {
        unsigned short numSectors;
        unsigned short sectorSize;
        unsigned char hostSlot;
        unsigned char deviceSlot;
        char filename[MAX_FILENAME_LEN]; // WIll set this to MAX_FILENAME_LEN, later.
    } newDisk;

    // Ask for details on the new disk to create
    uint8_t ck = bus_to_peripheral((uint8_t *)&newDisk, sizeof(newDisk));

    if (ck != bus_checksum((uint8_t *)&newDisk, sizeof(newDisk)))
    {
        Debug_print("bus_new_disk Bad checksum\n");
        bus_error();
        return;
    }
    if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    {
        Debug_print("bus_new_disk Bad disk or host slot parameter\n");
        bus_error();
        return;
    }
    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[newDisk.deviceSlot];
    fujiHost &host = _fnHosts[newDisk.hostSlot];

    disk.host_slot = newDisk.hostSlot;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, newDisk.filename, sizeof(disk.filename));

    if (host.file_exists(disk.filename))
    {
        Debug_printf("bus_new_disk File exists: \"%s\"\n", disk.filename);
        bus_error();
        return;
    }

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");
    if (disk.fileh == nullptr)
    {
        Debug_printf("bus_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        bus_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    fclose(disk.fileh);

    if (ok == false)
    {
        Debug_print("bus_new_disk Data write failed\n");
        bus_error();
        return;
    }

    Debug_print("bus_new_disk succeeded\n");
    bus_complete();
}

// Send host slot data to computer
void fujiDevice::bus_read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    bus_to_computer((uint8_t *)&hostSlots, sizeof(hostSlots), false);
}

// Read and save host slot data from computer
void fujiDevice::bus_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    uint8_t ck = bus_to_peripheral((uint8_t *)&hostSlots, sizeof(hostSlots));

    if (bus_checksum((uint8_t *)hostSlots, sizeof(hostSlots)) == ck)
    {
        for (int i = 0; i < MAX_HOSTS; i++)
            _fnHosts[i].set_hostname(hostSlots[i]);

        _populate_config_from_slots();
        Config.save();

        bus_complete();
    }
    else
        bus_error();
}

// Store host path prefix
void fujiDevice::bus_set_host_prefix()
{
    char prefix[MAX_HOST_PREFIX_LEN];
    uint8_t hostSlot = cmdFrame.aux1;

    uint8_t ck = bus_to_peripheral((uint8_t *)prefix, MAX_FILENAME_LEN);

    Debug_printf("Fuji cmd: SET HOST PREFIX %uh \"%s\"\n", hostSlot, prefix);

    if (bus_checksum((uint8_t *)prefix, sizeof(prefix)) != ck)
    {
        bus_error();
        return;
    }

    if (!_validate_host_slot(hostSlot))
    {
        bus_error();
        return;
    }

    _fnHosts[hostSlot].set_prefix(prefix);
    bus_complete();
}

// Retrieve host path prefix
void fujiDevice::bus_get_host_prefix()
{
    uint8_t hostSlot = cmdFrame.aux1;
    Debug_printf("Fuji cmd: GET HOST PREFIX %uh\n", hostSlot);

    if (!_validate_host_slot(hostSlot))
    {
        bus_error();
        return;
    }
    char prefix[MAX_HOST_PREFIX_LEN];
    _fnHosts[hostSlot].get_prefix(prefix, sizeof(prefix));

    bus_to_computer((uint8_t *)prefix, sizeof(prefix), false);
}

// Send device slot data to computer
void fujiDevice::bus_read_device_slots()
{
    Debug_println("Fuji cmd: READ DEVICE SLOTS");

    struct disk_slot
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    };
    disk_slot diskSlots[MAX_DISK_DEVICES];

    int returnsize;

    // AUX1 specifies which slots to return
    // Handle disk slots
    if (cmdFrame.aux1 == READ_DEVICE_SLOTS_DISKS1)
    {
        // Load the data from our current device array
        for (int i = 0; i < MAX_DISK_DEVICES; i++)
        {
            diskSlots[i].mode = _fnDisks[i].access_mode;
            diskSlots[i].hostSlot = _fnDisks[i].host_slot;
            strlcpy(diskSlots[i].filename, _fnDisks[i].filename, MAX_DISPLAY_FILENAME_LEN);
        }

        returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;
    }
    // Handle tape slot
    // else if (cmdFrame.aux1 == READ_DEVICE_SLOTS_TAPE)
    // {
    //     // TODO: Populate this with real values
    //     // TODO: allow read and write
    //     // TODO: why [0] and not [8] (device 9)?
    //     diskSlots[0].mode = 0; // Always READ
    //     diskSlots[0].hostSlot = 0;
    //     strlcpy(diskSlots[0].filename, "TAPETEST.CAS", MAX_DISPLAY_FILENAME_LEN);

    //     returnsize = sizeof(disk_slot);
    // }
    // Bad AUX1 value
    else
    {
        bus_error();
        return;
    }

    bus_to_computer((uint8_t *)&diskSlots, returnsize, false);
}

// Read and save disk slot data from computer
void fujiDevice::bus_write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    struct
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    } diskSlots[MAX_DISK_DEVICES];

    uint8_t ck = bus_to_peripheral((uint8_t *)&diskSlots, sizeof(diskSlots));

    if (ck == bus_checksum((uint8_t *)&diskSlots, sizeof(diskSlots)))
    {
        // Load the data into our current device array
        for (int i = 0; i < MAX_DISK_DEVICES; i++)
            _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

        // Save the data to disk
        _populate_config_from_slots();
        Config.save();

        bus_complete();
    }
    else
        bus_error();
}

// Temporary(?) function while we move from old config storage to new
void fujiDevice::_populate_slots_from_config()
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
void fujiDevice::_populate_config_from_slots()
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

// AUX1 is our index value (from 0 to BUS_HISPEED_LOWEST_INDEX)
// AUX2 requests a save of the change if set to 1
void fujiDevice::bus_set_hbus_index()
{
    Debug_println("Fuji cmd: SET HSIO INDEX");

    // DAUX1 holds the desired index value
    uint8_t index = cmdFrame.aux1;

    // Make sure it's a valid value
    if (index > BUS_HISPEED_LOWEST_INDEX)
    {
        bus_error();
        return;
    }

    SIO.setHighSpeedIndex(index);

    // Go ahead and save it if AUX2 = 1
    if (cmdFrame.aux2 & 1)
    {
        Config.store_general_hsioindex(index);
        Config.save();
    }

    bus_complete();
}

// Write a 256 byte filename to the device slot
void fujiDevice::bus_set_device_filename()
{
    char tmp[MAX_FILENAME_LEN];

    // AUX1 is the desired device slot
    uint8_t slot = cmdFrame.aux1;
    // AUX2 contains the host slot and the mount mode (READ/WRITE)
    uint8_t host = cmdFrame.aux2 >> 4;
    uint8_t mode = cmdFrame.aux2 & 0x0F;

    uint8_t ck = bus_to_peripheral((uint8_t *)tmp, MAX_FILENAME_LEN);

    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", slot, host, mode, tmp);

    if (bus_checksum((uint8_t *)tmp, MAX_FILENAME_LEN) != ck)
    {
        bus_error();
        return;
    }

    // Handle DISK slots
    if (slot < MAX_DISK_DEVICES)
    {
        // TODO: Set HOST and MODE
        memcpy(_fnDisks[cmdFrame.aux1].filename, tmp, MAX_FILENAME_LEN);
        _populate_config_from_slots();
    }
    // Handle TAPE slots
    // else if (slot == BASE_TAPE_SLOT) // TODO? currently do not use this option for CAS image filenames
    // {
    //     // Just save the filename until we need it mount the tape
    //     // TODO: allow read and write options
    //     Config.store_mount(0, host, tmp, fnConfig::mount_mode_t::MOUNTMODE_READ, fnConfig::MOUNTTYPE_TAPE);
    // }
    // Bad slot
    else
    {
        Debug_println("BAD DEVICE SLOT");
        bus_error();
        return;
    }

    Config.save();
    bus_complete();
}

// Get a 256 byte filename from device slot
void fujiDevice::bus_get_device_filename()
{
    char tmp[MAX_FILENAME_LEN];
    unsigned char err = false;

    // AUX1 is the desired device slot
    uint8_t slot = cmdFrame.aux1;

    if (slot > 7)
    {
        err = true;
    }

    memcpy(tmp, _fnDisks[cmdFrame.aux1].filename, MAX_FILENAME_LEN);
    bus_to_computer((uint8_t *)tmp, MAX_FILENAME_LEN, err);
}

// Set an external clock rate in kHz defined by aux1/aux2, aux2 in steps of 2kHz.
void fujiDevice::bus_set_bus_external_clock()
{
    unsigned short speed = bus_get_aux();
    int baudRate = speed * 1000;

    Debug_printf("fujiDevice::bus_set_external_clock(%u)\n", baudRate);

    if (speed == 0)
    {
        SIO.setUltraHigh(false, 0);
    }
    else
    {
        SIO.setUltraHigh(true, baudRate);
    }

    bus_complete();
}

// Mounts the desired boot disk number
void fujiDevice::insert_boot_device(uint8_t d)
{
    const char *config_atr = "/autorun.atr";
    const char *mount_all_atr = "/mount-and-boot.atr";
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

// Initializes base settings and adds our devices to the bus
void fujiDevice::setup(systemBus *bus)
{
    // set up Fuji device
    _bus = bus;

    _populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode());

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();
    
    //Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();

    // Add our devices to the SIO bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _bus->addDevice(&_fnDisks[i].disk_dev, BUS_DEVICEID_DISK + i);

    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
        _bus->addDevice(&iecNetDevs[i], BUS_DEVICEID_FN_NETWORK + i);

    _bus->addDevice(&_cassetteDev, BUS_DEVICEID_CASSETTE);
    cassette()->set_buttons(Config.get_cassette_buttons());
    cassette()->set_pulldown(Config.get_cassette_pulldown());
}

iecDisk *fujiDevice::bootdisk()
{
    return &_bootDisk;
}

void fujiDevice::bus_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    Debug_println("fujiDevice::bus_process() called");

    switch (cmdFrame.comnd)
    {
    case BUS_FUJICMD_HSIO_INDEX:
        bus_ack();
        bus_high_speed();
        break;
    case BUS_FUJICMD_SET_HSIO_INDEX:
        bus_ack();
        bus_set_hbus_index();
        break;
    case BUS_FUJICMD_STATUS:
        bus_ack();
        bus_status();
        break;
    case BUS_FUJICMD_RESET:
        bus_ack();
        bus_reset_fujinet();
        break;
    case BUS_FUJICMD_SCAN_NETWORKS:
        bus_ack();
        bus_net_scan_networks();
        break;
    case BUS_FUJICMD_GET_SCAN_RESULT:
        bus_ack();
        bus_net_scan_result();
        break;
    case BUS_FUJICMD_SET_SSID:
        bus_ack();
        bus_net_set_ssid();
        break;
    case BUS_FUJICMD_GET_SSID:
        bus_ack();
        bus_net_get_ssid();
        break;
    case BUS_FUJICMD_GET_WIFISTATUS:
        bus_ack();
        bus_net_get_wifi_status();
        break;
    case BUS_FUJICMD_MOUNT_HOST:
        bus_ack();
        bus_mount_host();
        break;
    case BUS_FUJICMD_MOUNT_IMAGE:
        bus_ack();
        bus_disk_image_mount();
        break;
    case BUS_FUJICMD_OPEN_DIRECTORY:
        bus_ack();
        bus_open_directory();
        break;
    case BUS_FUJICMD_READ_DIR_ENTRY:
        bus_ack();
        bus_read_directory_entry();
        break;
    case BUS_FUJICMD_CLOSE_DIRECTORY:
        bus_ack();
        bus_close_directory();
        break;
    case BUS_FUJICMD_GET_DIRECTORY_POSITION:
        bus_ack();
        bus_get_directory_position();
        break;
    case BUS_FUJICMD_SET_DIRECTORY_POSITION:
        bus_ack();
        bus_set_directory_position();
        break;
    case BUS_FUJICMD_READ_HOST_SLOTS:
        bus_ack();
        bus_read_host_slots();
        break;
    case BUS_FUJICMD_WRITE_HOST_SLOTS:
        bus_ack();
        bus_write_host_slots();
        break;
    case BUS_FUJICMD_READ_DEVICE_SLOTS:
        bus_ack();
        bus_read_device_slots();
        break;
    case BUS_FUJICMD_WRITE_DEVICE_SLOTS:
        bus_ack();
        bus_write_device_slots();
        break;
    case BUS_FUJICMD_UNMOUNT_IMAGE:
        bus_ack();
        bus_disk_image_umount();
        break;
    case BUS_FUJICMD_GET_ADAPTERCONFIG:
        bus_ack();
        bus_get_adapter_config();
        break;
    case BUS_FUJICMD_NEW_DISK:
        bus_ack();
        bus_new_disk();
        break;
    case BUS_FUJICMD_SET_DEVICE_FULLPATH:
        bus_ack();
        bus_set_device_filename();
        break;
    case BUS_FUJICMD_SET_HOST_PREFIX:
        bus_ack();
        bus_set_host_prefix();
        break;
    case BUS_FUJICMD_GET_HOST_PREFIX:
        bus_ack();
        bus_get_host_prefix();
        break;
    case BUS_FUJICMD_SET_SIO_EXTERNAL_CLOCK:
        bus_ack();
        bus_set_bus_external_clock();
        break;
    case BUS_FUJICMD_WRITE_APPKEY:
        bus_ack();
        bus_write_app_key();
        break;
    case BUS_FUJICMD_READ_APPKEY:
        bus_ack();
        bus_read_app_key();
        break;
    case BUS_FUJICMD_OPEN_APPKEY:
        bus_ack();
        bus_open_app_key();
        break;
    case BUS_FUJICMD_CLOSE_APPKEY:
        bus_ack();
        bus_close_app_key();
        break;
    case BUS_FUJICMD_GET_DEVICE_FULLPATH:
        bus_ack();
        bus_get_device_filename();
        break;
    case BUS_FUJICMD_CONFIG_BOOT:
        bus_ack();
        bus_set_boot_config();
        break;
    case BUS_FUJICMD_COPY_FILE:
        bus_ack();
        bus_copy_file();
        break;
    case BUS_FUJICMD_MOUNT_ALL:
        bus_ack();
        bus_mount_all();
        break;
    case BUS_FUJICMD_SET_BOOT_MODE:
        bus_ack();
        bus_set_boot_mode();
        break;
    default:
        bus_nak();
    }
}

int fujiDevice::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string fujiDevice::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

#endif /* BUILD_CBM */