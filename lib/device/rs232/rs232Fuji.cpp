#ifdef BUILD_RS232

#include "rs232Fuji.h"
#include "fujiCommandID.h"

#include <cstdint>
#include <cstring>

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"
#include "fnWiFi.h"

#include "led.h"
#include "utils.h"
#include "string_utils.h"
#include "compat_string.h"

#ifndef ESP_PLATFORM // why ESP does not like it? it throws a linker error undefined reference to 'basename'
#include <libgen.h>
#endif /* ESP_PLATFORM */

rs232Fuji platformFuji;
rs232Fuji *theFuji = &platformFuji; // global fuji device object

//rs232Disk rs232DiskDevs[MAX_HOSTS];
rs232Network rs232NetDevs[MAX_NETWORK_DEVICES];

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
}

/**
 * Say swap label
 */
void say_swap_label()
{
}

// Constructor
rs232Fuji::rs232Fuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void rs232Fuji::rs232_status()
{
    Debug_println("Fuji cmd: STATUS");

    if (cmdFrame.aux == STATUS_MOUNT_TIME)
    {
        // Return drive slot mount status: 0 if unmounted, otherwise time when mounted
        time_t mount_status[MAX_DISK_DEVICES];
        int idx;


        for (idx = 0; idx < MAX_DISK_DEVICES; idx++)
            mount_status[idx] = _fnDisks[idx].disk_dev.mount_time();

        bus_to_computer((uint8_t *) mount_status, sizeof(mount_status), false);
    }
    else
    {
        char ret[4] = {0};

        Debug_printf("Status for what? %08lx\n", cmdFrame.aux);
        bus_to_computer((uint8_t *)ret, sizeof(ret), false);
    }
    return;
}

// Reset FujiNet
void rs232Fuji::rs232_reset_fujinet()
{
    Debug_println("Fuji cmd: REBOOT");
    rs232_complete();
    fnSystem.reboot();
}

// Scan for networks
void rs232Fuji::rs232_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    char ret[4] = {0};

    _countScannedSSIDs = fnWiFi.scan_networks();

    ret[0] = _countScannedSSIDs;

    bus_to_computer((uint8_t *)ret, 4, false);
}

// Return scanned network entry
void rs232Fuji::rs232_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");

    // Response to  FUJICMD_GET_SCAN_RESULT
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
void rs232Fuji::rs232_net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    // Response to  FUJICMD_GET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    /*
     We memcpy instead of strcpy because technically the SSID and phasephras aren't std::strings and aren't null terminated,
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
void rs232Fuji::rs232_net_set_ssid()
{
    Debug_println("Fuji cmd: SET SSID");

    // Data for  FUJICMD_SET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    uint8_t ck = bus_to_peripheral((uint8_t *)&cfg, sizeof(cfg));

    if (rs232_checksum((uint8_t *)&cfg, sizeof(cfg)) != ck)
        rs232_error();
    else
    {
        bool save = cmdFrame.aux1 != 0;

        // URL Decode SSID/PASSWORD to handle special chars
        //mstr::urlDecode(cfg.ssid, sizeof(cfg.ssid));
        //mstr::urlDecode(cfg.password, sizeof(cfg.password));

        Debug_printf("Connecting to net: %s password: %s\n", cfg.ssid, cfg.password);

        fnWiFi.connect(cfg.ssid, cfg.password);

        // Only save these if we're asked to, otherwise assume it was a test for connectivity
        if (save)
        {
            Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
            Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
            Config.save();
        }

        rs232_complete();
    }
}

// Get WiFi Status
void rs232Fuji::rs232_net_get_wifi_status()
{
    Debug_println("Fuji cmd: GET WIFI STATUS");
    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    bus_to_computer(&wifiStatus, sizeof(wifiStatus), false);
}

// Check if Wifi is enabled
void rs232Fuji::rs232_net_get_wifi_enabled()
{
    uint8_t e = Config.get_wifi_enabled() ? 1 : 0;
    Debug_printf("Fuji cmd: GET WIFI ENABLED: %d\n",e);
    bus_to_computer(&e, sizeof(e), false);
}

// Mount Server
void rs232Fuji::rs232_mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = cmdFrame.aux1;

    // Make sure we weren't given a bad hostSlot
    if (!_validate_host_slot(hostSlot, "rs232_tnfs_mount_hosts"))
    {
        rs232_error();
        return;
    }

    if (!_fnHosts[hostSlot].mount())
        rs232_error();
    else
        rs232_complete();
}

// Disk Image Mount
void rs232Fuji::rs232_disk_image_mount()
{
    // TAPE or CASSETTE handling: this function can also mount CAS and WAV files
    // to the C: device. Everything stays the same here and the mounting
    // where all the magic happens is done in the rs232Disk::mount() function.
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
        rs232_error();
        return;
    }

    if (!_validate_host_slot(_fnDisks[deviceSlot].host_slot))
    {
        rs232_error();
        return;
    }

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 disk.filename, disk.host_slot, flag, deviceSlot + 1);

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    if (disk.fileh == nullptr)
    {
        rs232_error();
        return;
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;
    status_wait_count = 0;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);

    rs232_complete();
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void rs232Fuji::rs232_set_boot_config()
{
    boot_config = cmdFrame.aux1;
    rs232_complete();
}

// Do RS232 copy
void rs232Fuji::rs232_copy_file()
{
    uint8_t csBuf[256];
    std::string copySpec;
    std::string sourcePath;
    std::string destPath;
    uint8_t ck;
    fnFile *sourceFile;
    fnFile *destFile;
    char *dataBuf;
    unsigned char sourceSlot;
    unsigned char destSlot;

    dataBuf = (char *)malloc(532);

    if (dataBuf == nullptr)
    {
        rs232_error();
        return;
    }

    memset(&csBuf, 0, sizeof(csBuf));

    ck = bus_to_peripheral(csBuf, sizeof(csBuf));

    if (ck != rs232_checksum(csBuf, sizeof(csBuf)))
    {
        rs232_error();
        free(dataBuf);
        return;
    }

    copySpec = std::string((char *)csBuf);

    Debug_printf("copySpec: %s\n", copySpec.c_str());

    // Check for malformed copyspec.
    if (copySpec.empty() || copySpec.find_first_of("|") == std::string::npos)
    {
        rs232_error();
        free(dataBuf);
        return;
    }

    if (cmdFrame.aux1 < 1 || cmdFrame.aux1 > 8)
    {
        rs232_error();
        free(dataBuf);
        return;
    }

    if (cmdFrame.aux2 < 1 || cmdFrame.aux2 > 8)
    {
        rs232_error();
        free(dataBuf);
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
        std::string sourceFilename = sourcePath.substr(sourcePath.find_last_of("/") + 1);
        destPath += sourceFilename;
    }

    // Mount hosts, if needed.
    _fnHosts[sourceSlot].mount();
    _fnHosts[destSlot].mount();

    // Open files...
    sourceFile = _fnHosts[sourceSlot].fnfile_open(sourcePath.c_str(), (char *)sourcePath.c_str(), sourcePath.size() + 1, "r");

    if (sourceFile == nullptr)
    {
        rs232_error();
        free(dataBuf);
        return;
    }

    destFile = _fnHosts[destSlot].fnfile_open(destPath.c_str(), (char *)destPath.c_str(), destPath.size() + 1, "w");

    if (destFile == nullptr)
    {
        rs232_error();
        fnio::fclose(sourceFile);
        free(dataBuf);
        return;
    }

    size_t count = 0;
    do
    {
        count = fnio::fread(dataBuf, 1, 532, sourceFile);
        fnio::fwrite(dataBuf, 1, count, destFile);
    } while (count > 0);

    rs232_complete();

    // copyEnd:
    fnio::fclose(sourceFile);
    fnio::fclose(destFile);
    free(dataBuf);
}

// Mount all
void rs232Fuji::mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < 8; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT && strlen(disk.filename) > 0)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                rs232_error();
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                rs232_error();
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

    rs232_complete();
}

// Set boot mode
void rs232Fuji::rs232_set_boot_mode()
{
    insert_boot_device(cmdFrame.aux1);
    boot_config = true;
    rs232_complete();
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
void rs232Fuji::rs232_open_app_key()
{
    Debug_print("Fuji cmd: OPEN APPKEY\n");

    // The data expected for this command
    uint8_t ck = bus_to_peripheral((uint8_t *)&_current_appkey, sizeof(_current_appkey));

    if (rs232_checksum((uint8_t *)&_current_appkey, sizeof(_current_appkey)) != ck)
    {
        rs232_error();
        return;
    }

    // We're only supporting writing to SD, so return an error if there's no SD mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        rs232_error();
        return;
    }

    // Basic check for valid data
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        rs232_error();
        return;
    }

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, filename = \"%s\"\n",
                 _current_appkey.creator, _current_appkey.app, _current_appkey.key, _current_appkey.mode,
                 _generate_appkey_filename(&_current_appkey));

    rs232_complete();
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void rs232Fuji::rs232_close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    rs232_complete();
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void rs232Fuji::rs232_write_app_key()
{
    uint16_t keylen = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);

    Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\n", keylen);

    // Data for  FUJICMD_WRITE_APPKEY
    uint8_t value[MAX_APPKEY_LEN];

    uint8_t ck = bus_to_peripheral((uint8_t *)value, sizeof(value));

    if (rs232_checksum((uint8_t *)value, sizeof(value)) != ck)
    {
        rs232_error();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        rs232_error();
        return;
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        rs232_error();
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
        rs232_error();
        return;
    }
    size_t count = fwrite(value, 1, keylen, fOut);
    int e = errno;

    fclose(fOut);

    if (count != keylen)
    {
        Debug_printf("Only wrote %u bytes of expected %hu, errno=%d\n", count, keylen, e);
        rs232_error();
    }

    rs232_complete();
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void rs232Fuji::rs232_read_app_key()
{

    Debug_println("Fuji cmd: READ APPKEY");

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        rs232_error();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_READ)
    {
        Debug_println("Invalid app key metadata - aborting");
        rs232_error();
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    Debug_printf("Reading appkey from \"%s\"\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, "r");
    if (fIn == nullptr)
    {
        Debug_printf("Failed to open input file: errno=%d\n", errno);
        rs232_error();
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
void rs232Fuji::debug_tape()
{
}

// Disk Image Unmount
void rs232Fuji::rs232_disk_image_umount()
{
    uint8_t deviceSlot = cmdFrame.aux1;

    Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // Handle disk slots
    if (deviceSlot < MAX_DISK_DEVICES)
    {
        _fnDisks[deviceSlot].disk_dev.unmount();
        _fnDisks[deviceSlot].reset();
    }
    // Handle tape
    // else if (deviceSlot == BASE_TAPE_SLOT)
    // {
    // }
    // Invalid slot
    else
    {
        rs232_error();
        return;
    }

    rs232_complete();
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void rs232Fuji::image_rotate()
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
        fujiDeviceID_t last_id = _fnDisks[count].disk_dev.id();

        for (int n = count; n > 0; n--)
        {
            fujiDeviceID_t swap = _fnDisks[n - 1].disk_dev.id();
            Debug_printf("setting slot %d to ID %hx\n", n, swap);
            SYSTEM_BUS.changeDeviceId(&_fnDisks[n].disk_dev, swap);
        }

        // The first slot gets the device ID of the last slot
        SYSTEM_BUS.changeDeviceId(&_fnDisks[0].disk_dev, last_id);

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
void rs232Fuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

void rs232Fuji::rs232_open_directory()
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    char dirpath[256];
    uint8_t hostSlot = cmdFrame.aux1;
    uint8_t ck = bus_to_peripheral((uint8_t *)&dirpath, sizeof(dirpath));

    if (rs232_checksum((uint8_t *)&dirpath, sizeof(dirpath)) != ck)
    {
        rs232_error();
        return;
    }
    if (!_validate_host_slot(hostSlot))
    {
        rs232_error();
        return;
    }

    // If we already have a directory open, close it first
    if (_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closing it first\n");
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
        rs232_complete();
    }
    else
        rs232_error();
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

void rs232Fuji::rs232_read_directory_entry()
{
    uint8_t maxlen = cmdFrame.aux1;
    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        rs232_error();
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

void rs232Fuji::rs232_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        rs232_error();
        return;
    }

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        rs232_error();
        return;
    }
    // Return the value we read
    bus_to_computer((uint8_t *)&pos, sizeof(pos), false);
}

void rs232Fuji::rs232_set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // DAUX1 and DAUX2 hold the position to seek to in low/high order
    uint16_t pos = rs232_get_aux16_lo();

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        rs232_error();
        return;
    }

    bool result = _fnHosts[_current_open_directory_slot].dir_seek(pos);
    if (result == false)
    {
        rs232_error();
        return;
    }
    rs232_complete();
}

void rs232Fuji::rs232_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    rs232_complete();
}

// Get network adapter configuration
void rs232Fuji::rs232_get_adapter_config()
{
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    // Response to  FUJICMD_GET_ADAPTERCONFIG
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
void rs232Fuji::rs232_new_disk()
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

    if (ck != rs232_checksum((uint8_t *)&newDisk, sizeof(newDisk)))
    {
        Debug_print("rs232_new_disk Bad checksum\n");
        rs232_error();
        return;
    }
    if (newDisk.deviceSlot >= MAX_DISK_DEVICES || newDisk.hostSlot >= MAX_HOSTS)
    {
        Debug_print("rs232_new_disk Bad disk or host slot parameter\n");
        rs232_error();
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
        Debug_printf("rs232_new_disk File exists: \"%s\"\n", disk.filename);
        rs232_error();
        return;
    }

    disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), "w");
    if (disk.fileh == nullptr)
    {
        Debug_printf("rs232_new_disk Couldn't open file for writing: \"%s\"\n", disk.filename);
        rs232_error();
        return;
    }

    bool ok = disk.disk_dev.write_blank(disk.fileh, newDisk.sectorSize, newDisk.numSectors);
    fnio::fclose(disk.fileh);

    if (ok == false)
    {
        Debug_print("rs232_new_disk Data write failed\n");
        rs232_error();
        return;
    }

    Debug_print("rs232_new_disk succeeded\n");
    rs232_complete();
}

// Send host slot data to computer
void rs232Fuji::rs232_read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    bus_to_computer((uint8_t *)&hostSlots, sizeof(hostSlots), false);
}

// Read and save host slot data from computer
void rs232Fuji::rs232_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    uint8_t ck = bus_to_peripheral((uint8_t *)&hostSlots, sizeof(hostSlots));

    if (rs232_checksum((uint8_t *)hostSlots, sizeof(hostSlots)) == ck)
    {
        for (int i = 0; i < MAX_HOSTS; i++)
            _fnHosts[i].set_hostname(hostSlots[i]);

        _populate_config_from_slots();
        Config.save();

        rs232_complete();
    }
    else
        rs232_error();
}

// Store host path prefix
void rs232Fuji::rs232_set_host_prefix()
{
    char prefix[MAX_HOST_PREFIX_LEN];
    uint8_t hostSlot = cmdFrame.aux1;

    uint8_t ck = bus_to_peripheral((uint8_t *)prefix, MAX_HOST_PREFIX_LEN);

    Debug_printf("Fuji cmd: SET HOST PREFIX %uh \"%s\"\n", hostSlot, prefix);

    if (rs232_checksum((uint8_t *)prefix, sizeof(prefix)) != ck)
    {
        rs232_error();
        return;
    }

    if (!_validate_host_slot(hostSlot))
    {
        rs232_error();
        return;
    }

    _fnHosts[hostSlot].set_prefix(prefix);
    rs232_complete();
}

// Retrieve host path prefix
void rs232Fuji::rs232_get_host_prefix()
{
    uint8_t hostSlot = cmdFrame.aux1;
    Debug_printf("Fuji cmd: GET HOST PREFIX %uh\n", hostSlot);

    if (!_validate_host_slot(hostSlot))
    {
        rs232_error();
        return;
    }
    char prefix[MAX_HOST_PREFIX_LEN];
    _fnHosts[hostSlot].get_prefix(prefix, sizeof(prefix));

    bus_to_computer((uint8_t *)prefix, sizeof(prefix), false);
}

// Send device slot data to computer
void rs232Fuji::rs232_read_device_slots()
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
    char *filename;

    // AUX1 specifies which slots to return
    // Handle disk slots
    if (cmdFrame.aux1 == READ_DEVICE_SLOTS_DISKS1)
    {
        // Load the data from our current device array
        for (int i = 0; i < MAX_DISK_DEVICES; i++)
        {
            diskSlots[i].mode = _fnDisks[i].access_mode;
            diskSlots[i].hostSlot = _fnDisks[i].host_slot;
            if ( _fnDisks[i].filename[0] == '\0' )
            {
                strlcpy(diskSlots[i].filename, "", MAX_DISPLAY_FILENAME_LEN);
            }
            else
            {
                // Just use the basename of the image, no path. The full path+filename is
                // usually too long for the Atari to show anyway, so the image name is more important.
                // Note: Basename can modify the input, so use a copy of the filename
                filename = strdup(_fnDisks[i].filename);
                strlcpy ( diskSlots[i].filename, basename(filename), MAX_DISPLAY_FILENAME_LEN );
                free(filename);
            }
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
        rs232_error();
        return;
    }

    bus_to_computer((uint8_t *)&diskSlots, returnsize, false);
}

// Read and save disk slot data from computer
void rs232Fuji::rs232_write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    struct
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    } diskSlots[MAX_DISK_DEVICES];

    uint8_t ck = bus_to_peripheral((uint8_t *)&diskSlots, sizeof(diskSlots));

    if (ck == rs232_checksum((uint8_t *)&diskSlots, sizeof(diskSlots)))
    {
        // Load the data into our current device array
        for (int i = 0; i < MAX_DISK_DEVICES; i++)
            _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

        // Save the data to disk
        _populate_config_from_slots();
        Config.save();

        rs232_complete();
    }
    else
        rs232_error();
}

// Temporary(?) function while we move from old config storage to new
void rs232Fuji::_populate_slots_from_config()
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
void rs232Fuji::_populate_config_from_slots()
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
void rs232Fuji::rs232_set_device_filename()
{
    char tmp[MAX_FILENAME_LEN];

    // AUX1 is the desired device slot
    uint8_t slot = cmdFrame.aux1;
    // AUX2 contains the host slot and the mount mode (READ/WRITE)
    uint8_t host = cmdFrame.aux2 >> 4;
    uint8_t mode = cmdFrame.aux2 & 0x0F;

    uint8_t ck = bus_to_peripheral((uint8_t *)tmp, MAX_FILENAME_LEN);

    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", slot, host, mode, tmp);

    if (rs232_checksum((uint8_t *)tmp, MAX_FILENAME_LEN) != ck)
    {
        rs232_error();
        return;
    }

    // Handle DISK slots
    if (slot < MAX_DISK_DEVICES)
    {
        memcpy(_fnDisks[cmdFrame.aux1].filename, tmp, MAX_FILENAME_LEN);

        // If the filename is empty, mark this as an invalid host, so that mounting will ignore it too
        if (strlen(_fnDisks[cmdFrame.aux1].filename) == 0) {
            _fnDisks[cmdFrame.aux1].host_slot = INVALID_HOST_SLOT;
        } else {
            _fnDisks[cmdFrame.aux1].host_slot = host;
        }

        _fnDisks[cmdFrame.aux1].access_mode = mode;
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
        rs232_error();
        return;
    }

    Config.save();
    rs232_complete();
}

// Get a 256 byte filename from device slot
void rs232Fuji::rs232_get_device_filename()
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
void rs232Fuji::rs232_set_rs232_external_clock()
{
    unsigned short speed = rs232_get_aux16_lo();
    int baudRate = speed * 1000;

    Debug_printf("rs232Fuji::rs232_set_external_clock(%u)\n", baudRate);

    if (speed == 0)
    {
        SYSTEM_BUS.setUltraHigh(false, 0);
    }
    else
    {
        SYSTEM_BUS.setUltraHigh(true, baudRate);
    }

    rs232_complete();
}

// Mounts the desired boot disk number
void rs232Fuji::insert_boot_device(uint8_t d)
{
    const char *config_atr = "/autorun.img";
    const char *mount_all_atr = "/mount-and-boot.img";
    fnFile *fBoot = nullptr;

    Debug_printf("rs232Fuji::insert_boot_device(%u)\n",d);

    _bootDisk.unmount();

    switch (d)
    {
    case 0:
        fBoot = fsFlash.fnfile_open(config_atr);
        _bootDisk.mount(fBoot, config_atr, 368640);
        break;
    case 1:
        fBoot = fsFlash.fnfile_open(mount_all_atr);
        _bootDisk.mount(fBoot, mount_all_atr, 368640);
        break;
    }

    Debug_printf("Mounted.\n");

    _bootDisk.is_config_device = true;
    _bootDisk.device_active = false;
}

// Set UDP Stream HOST & PORT and start it
void rs232Fuji::rs232_enable_udpstream()
{
    char host[64];

    uint8_t ck = bus_to_peripheral((uint8_t *)&host, sizeof(host));

    if (rs232_checksum((uint8_t *)&host, sizeof(host)) != ck)
        rs232_error();
    else
    {
        int port = (cmdFrame.aux1 << 8) | cmdFrame.aux2;

        Debug_printf("Fuji cmd ENABLE UDPSTREAM: HOST:%s PORT: %d\n", host, port);

        // Save the host and port
        Config.store_udpstream_host(host);
        Config.store_udpstream_port(port);
        Config.save();

        rs232_complete();

        // Start the UDP Stream
        SYSTEM_BUS.setUDPHost(host, port);
    }
}

// Initializes base settings and adds our devices to the RS232 bus
void rs232Fuji::setup()
{
    // set up Fuji device
    _populate_slots_from_config();

    insert_boot_device(Config.get_general_boot_mode());

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = Config.get_general_config_enabled();

    //Disable status_wait if our settings say to turn it off
    status_wait_enabled = Config.get_general_status_wait_enabled();

    // Add our devices to the RS232 bus
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&_fnDisks[i].disk_dev, (fujiDeviceID_t) (FUJI_DEVICEID_DISK + i));

    for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&rs232NetDevs[i],
                             (fujiDeviceID_t) (FUJI_DEVICEID_NETWORK + i));

}

rs232Disk *rs232Fuji::bootdisk()
{
    return &_bootDisk;
}

void rs232Fuji::rs232_test()
{
    uint8_t buf[512];

    Debug_printf("rs232_test()\n");
    memset(buf,'A',512);
    bus_to_computer(buf,512,false);
}

void rs232Fuji::rs232_process(cmdFrame_t *cmd_ptr)
{
    Debug_println("rs232Fuji::rs232_process() called");

    cmdFrame = *cmd_ptr;
    switch (cmdFrame.comnd)
    {
    case FUJICMD_STATUS:
        rs232_ack();
        rs232_status();
        break;
    case FUJICMD_RESET:
        rs232_ack();
        rs232_reset_fujinet();
        break;
    case FUJICMD_SCAN_NETWORKS:
        rs232_ack();
        rs232_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        rs232_ack();
        rs232_net_scan_result();
        break;
    case FUJICMD_SET_SSID:
        rs232_ack();
        rs232_net_set_ssid();
        break;
    case FUJICMD_GET_SSID:
        rs232_ack();
        rs232_net_get_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        rs232_ack();
        rs232_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        rs232_ack();
        rs232_mount_host();
        break;
    case FUJICMD_MOUNT_IMAGE:
        rs232_ack();
        rs232_disk_image_mount();
        break;
    case FUJICMD_OPEN_DIRECTORY:
        rs232_ack();
        rs232_open_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        rs232_ack();
        rs232_read_directory_entry();
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        rs232_ack();
        rs232_close_directory();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        rs232_ack();
        rs232_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        rs232_ack();
        rs232_set_directory_position();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        rs232_ack();
        rs232_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        rs232_ack();
        rs232_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        rs232_ack();
        rs232_read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        rs232_ack();
        rs232_write_device_slots();
        break;
    case FUJICMD_GET_WIFI_ENABLED:
        rs232_ack();
        rs232_net_get_wifi_enabled();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        rs232_ack();
        rs232_disk_image_umount();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        rs232_ack();
        rs232_get_adapter_config();
        break;
    case FUJICMD_NEW_DISK:
        rs232_ack();
        rs232_new_disk();
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        rs232_ack();
        rs232_set_device_filename();
        break;
    case FUJICMD_SET_HOST_PREFIX:
        rs232_ack();
        rs232_set_host_prefix();
        break;
    case FUJICMD_GET_HOST_PREFIX:
        rs232_ack();
        rs232_get_host_prefix();
        break;
    case FUJICMD_WRITE_APPKEY:
        rs232_ack();
        rs232_write_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        rs232_ack();
        rs232_read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        rs232_ack();
        rs232_open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        rs232_ack();
        rs232_close_app_key();
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        rs232_ack();
        rs232_get_device_filename();
        break;
    case FUJICMD_CONFIG_BOOT:
        rs232_ack();
        rs232_set_boot_config();
        break;
    case FUJICMD_COPY_FILE:
        rs232_ack();
        rs232_copy_file();
        break;
    case FUJICMD_MOUNT_ALL:
        rs232_ack();
        mount_all();
        break;
    case FUJICMD_SET_BOOT_MODE:
        rs232_ack();
        rs232_set_boot_mode();
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        rs232_ack();
        rs232_enable_udpstream();
        break;
    case FUJICMD_DEVICE_READY:
        Debug_printf("FUJICMD DEVICE TEST\n");
        rs232_ack();
        rs232_test();
        break;
    default:
        rs232_nak();
    }
}

int rs232Fuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string rs232Fuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

// Public method to update host in specific slot
fujiHost *rs232Fuji::set_slot_hostname(int host_slot, char *hostname)
{
    _fnHosts[host_slot].set_hostname(hostname);
    _populate_config_from_slots();
    return &_fnHosts[host_slot];
}

#endif /* BUILD_RS232 */
