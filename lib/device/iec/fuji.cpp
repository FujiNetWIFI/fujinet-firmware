#ifdef BUILD_IEC

#include "fuji.h"

#include <driver/ledc.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include "string_utils.h"
#include "../../../include/petscii.h"

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"
#include "fnWiFi.h"
#include "network.h"
#include "led.h"
#include "siocpm.h"
#include "clock.h"
#include "utils.h"

iecFuji theFuji; // global fuji device object

// iecNetwork sioNetDevs[MAX_NETWORK_DEVICES];

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

// Reset FujiNet
void iecFuji::reset_fujinet()
{
    // TODO IMPLEMENT
    fnSystem.reboot();
}

// Scan for networks
void iecFuji::net_scan_networks()
{
    char c[8];

    _countScannedSSIDs = fnWiFi.scan_networks();

    if (payload[0] == FUJICMD_SCAN_NETWORKS)
    {
        response[0] = _countScannedSSIDs;
    }
    else
    {
        itoa(_countScannedSSIDs, c, 10);
        response = string(c);
    }
}

// Return scanned network entry
void iecFuji::net_scan_result()
{
    

    // pt[0] = SCANRESULT
    // pt[1] = scan result # (0-numresults)
    struct
    {
        char ssid[33];
        uint8_t rssi;
    } detail;

    if (pt.size() > 1)
    {
        util_remove_spaces(pt[1]);
        int i = atoi(pt[1].c_str());
        fnWiFi.get_scan_result(i, detail.ssid, &detail.rssi);
        Debug_printf("SSID: %s RSSI: %u\r\n", detail.ssid, detail.rssi);
    }
    else
    {
        strcpy(detail.ssid, "INVALID SSID");
        detail.rssi = 0;
    }

    if (payload[0] == FUJICMD_GET_SCAN_RESULT) // raw
    {
        std::string r = std::string((const char *)&detail, sizeof(detail));
        status_override = r;
    }
    else // SCANRESULT,n
    {
        char t[8];

        std::string s = std::string(detail.ssid);
        mstr::toPETSCII(s);
        itoa(detail.rssi, t, 10);

        response = std::string(t) + ",\"" + s + "\"";
    }
}

//  Get SSID
void iecFuji::net_get_ssid()
{
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        char password[64];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    std::string s = Config.get_wifi_ssid();
    memcpy(cfg.ssid, s.c_str(),
           s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(cfg.password, s.c_str(),
           s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

    if (payload[0] == FUJICMD_GET_SSID)
    {
        response = std::string((const char *)&cfg, sizeof(cfg));
    }
    else // BASIC mode.
    {
        response = std::string(cfg.ssid);
        mstr::toPETSCII(response);
    }
}

// Set SSID
void iecFuji::net_set_ssid()
{
    Debug_println("Fuji cmd: SET SSID");

    // Data for  FUJICMD_SET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        char password[64];
    } cfg;

    if (payload[0] == FUJICMD_SET_SSID)
    {
        strncpy((char *)&cfg, payload.substr(12, std::string::npos).c_str(), sizeof(cfg));
    }
    else // easy BASIC form
    {
        

        if (pt.size() == 3)
        {
            if ( mstr::isNumeric( pt[1] ) ) {
                // Find SSID by CRC8 Number
                pt[1] = fnWiFi.get_network_name_by_crc8( std::stoi(pt[1]) );
            }

            strncpy(cfg.ssid, pt[1].c_str(), 33);
            strncpy(cfg.password, pt[2].c_str(), 64);
        }
    }

    Debug_printf("Storing WiFi SSID and Password.\n");
    Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
    Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
    Config.save();

    Debug_printf("Connecting to net %s\n", cfg.ssid);
    fnWiFi.connect(cfg.ssid, cfg.password);

    iecStatus.channel = 15;
    iecStatus.error = 0;
    iecStatus.msg = "ssid set";
    iecStatus.connected = fnWiFi.connected();
}

// Get WiFi Status
void iecFuji::net_get_wifi_status()
{
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    char r[4];

    Debug_printv("payload[0]==%02x\n", payload[0]);

    if (payload[0] == FUJICMD_GET_WIFISTATUS)
    {
        response.clear();
        response = string((const char *)&wifiStatus,1);
        return;
    }
    else
    {
        response.clear();
        if (wifiStatus)
            response = "connected";
        else
            response = "disconnected";

        mstr::toPETSCII(response);
    }
}

// Check if Wifi is enabled
void iecFuji::net_get_wifi_enabled()
{
    // Not needed, will remove.
}

void iecFuji::unmount_host()
{
    int hs = -1;

    if (payload[0] == FUJICMD_UNMOUNT_HOST)
    {
        hs = payload[1];
    }
    else
    {
        
        if (pt.size() < 2) // send error.
        {
            response = "invalid # of parameters";
            return;
        }

        hs = atoi(pt[1].c_str());
    }

    if (!_validate_device_slot(hs, "unmount_host"))
    {
        response = "invalid device slot";
        return; // send error.
    }

    if (!_fnHosts[hs].umount())
    {
        response = "unable to unmount host slot";
        return; // send error;
    }

    response="ok";
}

// Mount Server
void iecFuji::mount_host()
{
    int hs = -1;

    _populate_slots_from_config();
    if (payload[0] == FUJICMD_MOUNT_HOST)
    {
        hs = payload[1];
    }
    else
    {
        

        if (pt.size() < 2) // send error.
        {
            response = "INVALID # OF PARAMETERS.";
            return;
        }

        hs = atoi(pt[1].c_str());
    }

    if (!_validate_device_slot(hs, "mount_host"))
    {
        response = "INVALID HOST HOST #";
        return; // send error.
    }

    if (!_fnHosts[hs].mount())
    {
        response = "UNABLE TO MOUNT HOST SLOT #";
        return; // send error.
    }

    // Otherwise, mount was successful.
    char hn[64];
    string hns;
    _fnHosts[hs].get_hostname(hn, 64);
    hns = string(hn);
    mstr::toPETSCII(hns);
    response = hns + " MOUNTED.";
}

// Disk Image Mount
void iecFuji::disk_image_mount()
{
    _populate_slots_from_config();
    
    if (pt.size() < 3)
    {
        response = "invalid # of parameters";
        return;
    }

    uint8_t ds = atoi(pt[1].c_str());
    uint8_t mode = atoi(pt[2].c_str());

    char flag[3] = {'r', 0, 0};

    if (mode == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    if (!_validate_device_slot(ds))
    {
        response = "invalid device slot.";
        return; // error.
    }

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[disk.host_slot];

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 disk.filename, disk.host_slot, flag, ds + 1);

    // TODO: Refactor along with mount disk image.
    disk.disk_dev.host = &host;

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    if (disk.fileh == nullptr)
    {
        response = "no file handle";
        return;
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
    response = "mounted";
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void iecFuji::set_boot_config()
{
    if (payload[0] == FUJICMD_CONFIG_BOOT)
    {
        boot_config = payload[1];
    }
    else
    {
        

        if (pt.size() < 2)
        {
            Debug_printf("Invalid # of parameters.\r\n");
            response = "invalid # of parameters";
            return;
        }

        boot_config = atoi(pt[1].c_str());
    }
    response = "ok";
}

// Do SIO copy
void iecFuji::copy_file()
{
    // TODO IMPLEMENT
}

// Mount all
void iecFuji::mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                // Send error.
                char slotno[3];
                itoa(i, slotno, 10);
                response = "error: unable to mount slot " + std::string(slotno) + "\r";
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                // Send error.
                char slotno[3];
                itoa(i, slotno, 10);
                response = "error: invalid file handle for slot " + std::string(slotno) + "\r";
                return;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;

            // We need the file size for loading XEX files and for CASSETTE, so get that too
            disk.disk_size = host.file_size(disk.fileh);

            // Set the host slot for high score mode
            // TODO: Refactor along with mount disk image.
            disk.disk_dev.host = &host;

            // And now mount it
            disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
        }
    }

    if (nodisks)
    {
        // No disks in a slot, disable config
        boot_config = false;
    }

    // Send successful.
    response = "ok";
}

// Set boot mode
void iecFuji::set_boot_mode()
{
    if (payload[0] == FUJICMD_CONFIG_BOOT)
    {
        boot_config = payload[1];
    }
    else
    {
        

        if (pt.size() < 2)
        {
            Debug_printf("Invalid # of parameters.\r\n");
            // send error
            response = "invalid # of parameters";
            return;
        }

        boot_config = true;
        insert_boot_device(atoi(pt[1].c_str()));
    }
    response = "ok";
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
    Debug_print("Fuji cmd: OPEN APPKEY\r\n");

    // The data expected for this command
    if (payload[0] == FUJICMD_OPEN_APPKEY)
        memcpy(&_current_appkey, &payload.c_str()[1], sizeof(_current_appkey));
    else
    {
        
        unsigned int val;

        if (pt.size() < 5)
        {
            Debug_printf("Incorrect number of parameters.\r\n");
            response = "invalid # of parameters";
            // send error.
        }

        sscanf(pt[1].c_str(), "%x", &val);
        _current_appkey.creator = (uint16_t)val;
        sscanf(pt[2].c_str(), "%x", &val);
        _current_appkey.app = (uint8_t)val;
        sscanf(pt[3].c_str(), "%x", &val);
        _current_appkey.key = (uint8_t)val;
        sscanf(pt[4].c_str(), "%x", &val);
        _current_appkey.mode = (appkey_mode)val;
        _current_appkey.reserved = 0;
    }

    // We're only supporting writing to SD, so return an error if there's no SD mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        // Send error
        response = "no sd card mounted";
        return;
    }

    // Basic check for valid data
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        // Send error.
        response = "invalid app key data";
        return;
    }

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, filename = \"%s\"\r\n",
                 _current_appkey.creator, _current_appkey.app, _current_appkey.key, _current_appkey.mode,
                 _generate_appkey_filename(&_current_appkey));

    // Send complete
    response = "ok";
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void iecFuji::close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\r\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    response = "ok";
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void iecFuji::write_app_key()
{
    uint16_t keylen = -1;
    char value[MAX_APPKEY_LEN];
    std::vector<std::string> ptRaw;

    // Binary mode comes through as-is (PETSCII)
    if (payload[0] == FUJICMD_WRITE_APPKEY)
    {
        keylen = payload[1] & 0xFF;
        keylen |= payload[2] << 8; 
        strncpy(value, &payload.c_str()[3], MAX_APPKEY_LEN);
    }
    else
    {   
        if (pt.size() > 2)
        {
            // Tokenize the raw PETSCII payload to save to the file
            ptRaw = tokenize_basic_command(payloadRaw);

            keylen = atoi(pt[1].c_str());
            
            // Bounds check
            if (keylen > MAX_APPKEY_LEN)
                keylen = MAX_APPKEY_LEN;

            strncpy(value, ptRaw[2].c_str(), keylen);
        }
        else
        {
            // send error
            response = "invalid # of parameters";
            return;
        }
    }

    Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\r\n", keylen);

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        // Send error
        response = "malformed appkey data.";
        return;
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        // Send error
        response = "no sd card mounted";
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    // Reset the app key data so we require calling APPKEY OPEN before another attempt
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;

    Debug_printf("Writing appkey to \"%s\"\r\n", filename);

    // Make sure we have a "/FujiNet" directory, since that's where we're putting these files
    fnSDFAT.create_path("/FujiNet");

    FILE *fOut = fnSDFAT.file_open(filename, "w");
    if (fOut == nullptr)
    {
        Debug_printf("Failed to open/create output file: errno=%d\r\n", errno);
        // Send error
        char e[8];
        itoa(errno, e, 10);
        response = "failed to write appkey file " + std::string(e);
        return;
    }
    size_t count = fwrite(value, 1, keylen, fOut);

    fclose(fOut);

    if (count != keylen)
    {
        char e[128];
        sprintf(e, "error: only wrote %u bytes of expected %hu, errno=%d\n", count, keylen, errno);
        response = std::string(e);
        // Send error
    }

    // Send ok
    response = "ok";
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void iecFuji::read_app_key()
{
    Debug_println("Fuji cmd: READ APPKEY");

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        response = "no sd mounted";
        // Send error
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_READ)
    {
        Debug_println("Invalid app key metadata - aborting");
        response = "invalid appkey metadata";
        // Send error
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    Debug_printf("Reading appkey from \"%s\"\r\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, "r");
    if (fIn == nullptr)
    {
        char e[128];
        sprintf(e, "Failed to open input file: errno=%d\r\n", errno);
        // Send error
        response = std::string(e);
        return;
    }

    struct
    {
        uint16_t size;
        uint8_t value[MAX_APPKEY_LEN];
    } __attribute__((packed)) _r;
    memset(&_r, 0, sizeof(_r));

    size_t count = fread(_r.value, 1, sizeof(_r.value), fIn);

    fclose(fIn);
    Debug_printf("Read %d bytes from input file\r\n", count);

    _r.size = count;

    // Bytes were written raw (PETSCII) so no need to map before sending back
    response = std::string((char *)&_r, MAX_APPKEY_LEN);
}

// Disk Image Unmount
void iecFuji::disk_image_umount()
{
    uint8_t deviceSlot = -1;

    if (payload[0] == FUJICMD_UNMOUNT_IMAGE)
    {
        deviceSlot = payload[1];
    }
    else
    {
        
        deviceSlot = atoi(pt[1].c_str());
    }

    Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);

    // Handle disk slots
    if (deviceSlot < MAX_DISK_DEVICES)
    {
        _fnDisks[deviceSlot].disk_dev.unmount();
        _fnDisks[deviceSlot].disk_dev.device_active = false;
        _fnDisks[deviceSlot].reset();
    }
    else
    {
        // Send error
        response = "invalid device slot";
        return;
    }
    response = "ok";
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
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    

    if (pt.size() < 3)
    {
        response = "invalid # of parameters.";
        return; // send error
    }

    char dirpath[256];
    uint8_t hostSlot = atoi(pt[1].c_str());
    strncpy(dirpath, pt[2].c_str(), sizeof(dirpath));

    if (!_validate_host_slot(hostSlot))
    {
        // send error
        response = "invalid host slot #";
        return;
    }

    // If we already have a directory open, close it first
    if (_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closign it first\n");
        _fnHosts[_current_open_directory_slot].dir_close();
        _current_open_directory_slot = -1;
    }

    // Preprocess ~ (pi!) into filter
    for (int i = 0; i < sizeof(dirpath); i++)
    {
        if (dirpath[i] == '~')
            dirpath[i] = 0; // turn into null
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
    }
    else
    {
        // send error
        response = "unable to open directory";
    }

    response = "ok";
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

void iecFuji::read_directory_entry()
{
    

    if (pt.size() < 2)
    {
        // send error
        response = "invalid # of parameters";
        return;
    }

    uint8_t maxlen = atoi(pt[1].c_str());
    uint8_t addtlopts = atoi(pt[2].c_str());

    Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        // Return error.
        response = "no currently open directory";
        Debug_print("No currently open directory\n");
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
        if (addtlopts & 0x80)
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

        // int filelen = strlcpy(filenamedest, f->filename, bufsize);
        int filelen = util_ellipsize(f->filename, filenamedest, bufsize);

        // Add a slash at the end of directory entries
        if (f->isDir && filelen < (bufsize - 2))
        {
            current_entry[filelen] = '/';
            current_entry[filelen + 1] = '\0';
        }
    }

    // Output RAW vs non-raw
    if (payload[0] == FUJICMD_READ_DIR_ENTRY)
        response = std::string((const char *)current_entry, maxlen);
    else
    {
        char reply[258];
        memset(reply, 0, sizeof(reply));
        sprintf(reply, "%s", current_entry);
        std::string s(reply);
        mstr::toPETSCII(s);
        response = s;
    }
}

void iecFuji::get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        response = "no currently open directory";
        // Send error
        return;
    }

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        Debug_println("Invalid directory position");
        // Send error.
        response = "invalid directory position";
        return;
    }

    // Return the value we read

    if (payload[0] == FUJICMD_GET_DIRECTORY_POSITION)
        response = std::string((const char *)&pos, sizeof(pos));
    else
    {
        char reply[8];
        itoa(pos, reply, 10);
        response = std::string(reply);
    }
}

void iecFuji::set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    uint16_t pos = 0;

    if (payload[0] == FUJICMD_SET_DIRECTORY_POSITION)
    {
        pos = payload[1] & 0xFF;
        pos |= payload[2] << 8;
    }
    else
    {
        
        if (pt.size() < 2)
        {
            Debug_println("Invalid directory position");
            response = "error: invalid directory position\r";
            return;
        }

        pos = atoi(pt[1].c_str());
    }

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        // Send error
        response = "error: no currently open directory";
        return;
    }

    bool result = _fnHosts[_current_open_directory_slot].dir_seek(pos);
    if (result == false)
    {
        // Send error
        response = "error: unable to perform directory seek\r";
        return;
    }
    response = "ok\r";
}

void iecFuji::close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    response = "ok";
}

// Get network adapter configuration
void iecFuji::get_adapter_config()
{
    Debug_printf("get_adapter_config()\r\n");

    memset(&cfg, 0, sizeof(cfg));

    strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

    if (!fnWiFi.connected())
    {
        strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
        strlcpy(cfg.hostname, "NOT CONNECTED", sizeof(cfg.hostname));
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

    if (payload[0] == FUJICMD_GET_ADAPTERCONFIG)
    {
        std::string reply = std::string((const char *)&cfg, sizeof(AdapterConfig));
        response = reply;
    }
    else if (payload == "ADAPTERCONFIG")
    {
        response = "use localip netmask gateway dnsip bssid hostname version";
        return;
    }
}

//  Make new disk and shove into device slot
void iecFuji::new_disk()
{
    // TODO: Implement when we actually have a good idea of
    // media types.
}

// Send host slot data to computer
void iecFuji::read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    if (payload[0] == FUJICMD_READ_HOST_SLOTS)
        response = std::string((const char *)hostSlots, 256);
    else
    {
        

        if (pt.size() < 2)
        {
            response = "host slot # required";
            return;
        }

        util_remove_spaces(pt[1]);

        int selected_hs = atoi(pt[1].c_str());
        std::string hn = std::string(hostSlots[selected_hs]);

        if (hn.empty())
        {
            response = "<empty>";
        }
        else
            response = std::string(hostSlots[selected_hs]);

        mstr::toPETSCII(response);
    }
}

// Read and save host slot data from computer
void iecFuji::write_host_slots()
{
    int hostSlot = -1;
    std::string hostname;

    Debug_println("FUJI CMD: WRITE HOST SLOTS");

    // RAW command
    if (payload[0] == FUJICMD_WRITE_HOST_SLOTS)
    {
        union _hostSlots
        {
            char hostSlots[8][32];
            char rawdata[256];
        } hostSlots;

        strncpy(hostSlots.rawdata, &payload.c_str()[1], sizeof(hostSlots.rawdata));

        for (int i = 0; i < MAX_HOSTS; i++)
        {
            _fnHosts[i].set_hostname(hostSlots.hostSlots[i]);

            _populate_config_from_slots();
            Config.save();
        }
    }
    else
    {
        // PUTHOST,<slot>,<hostname>
        

        if (pt.size() < 2)
        {
            response = "no host slot #";
            return;
        }
        else
            hostSlot = atoi(pt[1].c_str());

        if (!_validate_host_slot(hostSlot))
        {
            // Send error.
            response = "invalid host slot #";
            return;
        }

        if (pt.size() == 3)
        {
            hostname = pt[2];
        }
        else
        {
            hostname = std::string();
        }

        Debug_printf("Setting host slot %u to %s\n", hostSlot, hostname.c_str());
        _fnHosts[hostSlot].set_hostname(hostname.c_str());
    }

    _populate_config_from_slots();
    Config.save();

    response = "ok";
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

    // Load the data from our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        diskSlots[i].mode = _fnDisks[i].access_mode;
        diskSlots[i].hostSlot = _fnDisks[i].host_slot;
        if (_fnDisks[i].filename[0] == '\0')
        {
            strlcpy(diskSlots[i].filename, "", MAX_DISPLAY_FILENAME_LEN);
        }
        else
        {
            // Just use the basename of the image, no path. The full path+filename is
            // usually too long for the Atari to show anyway, so the image name is more important.
            // Note: Basename can modify the input, so use a copy of the filename
            filename = strdup(_fnDisks[i].filename);
            strlcpy(diskSlots[i].filename, basename(filename), MAX_DISPLAY_FILENAME_LEN);
            free(filename);
        }
    }

    returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;

    if (payload[0] == FUJICMD_READ_DEVICE_SLOTS)
        status_override = std::string((const char *)&diskSlots, returnsize);
    else
    {
        

        if (pt.size() < 2)
        {
            response = "host slot required";
            return;
        }

        util_remove_spaces(pt[1]);

        int selected_ds = atoi(pt[1].c_str());

        response = diskSlots[selected_ds].filename;
    }
}

// Read and save disk slot data from computer
void iecFuji::write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    if (payload[0] == FUJICMD_WRITE_DEVICE_SLOTS)
    {
        union _diskSlots
        {
            struct
            {
                uint8_t hostSlot;
                uint8_t mode;
                char filename[MAX_DISPLAY_FILENAME_LEN];
            } diskSlots[MAX_DISK_DEVICES];
            char rawData[152];
        } diskSlots;

        strncpy(diskSlots.rawData, &payload.c_str()[1], 152);

        // Load the data into our current device array
        for (int i = 0; i < MAX_DISK_DEVICES; i++)
            _fnDisks[i].reset(diskSlots.diskSlots[i].filename, diskSlots.diskSlots[i].hostSlot, diskSlots.diskSlots[i].mode);
    }
    else
    {
        // from BASIC
        

        if (pt.size() < 4)
        {
            response = "need file mode";
            return;
        }
        else if (pt.size() < 3)
        {
            response = "need filename";
            return;
        }
        else if (pt.size() < 2)
        {
            response = "need host slot";
            return;
        }
        else if (pt.size() < 1)
        {
            response = "need device slot";
            return;
        }

        unsigned char ds = atoi(pt[1].c_str());
        unsigned char hs = atoi(pt[2].c_str());
        string filename = pt[3];
        unsigned char m = atoi(pt[4].c_str());

        _fnDisks[ds].reset(filename.c_str(),hs,m);
        strncpy(_fnDisks[ds].filename,filename.c_str(),256);
    }

    // Save the data to disk
    _populate_config_from_slots();
    Config.save();
}

// Temporary(?) function while we move from old config storage to new
void iecFuji::_populate_slots_from_config()
{
    Debug_printf("_populate_slots_from_config()\n");
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
void iecFuji::_populate_config_from_slots()
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
void iecFuji::set_device_filename()
{
    char tmp[MAX_FILENAME_LEN];

    uint8_t slot = 0;
    uint8_t host = 0;
    uint8_t mode = 0;

    if (payload[0] == FUJICMD_SET_DEVICE_FULLPATH)
    {
        slot = payload[1];
        host = payload[2];
        mode = payload[3];
        strncpy(tmp, &payload[4], 256);
    }
    else
    {
        
        if (pt.size() < 4)
        {
            Debug_printf("not enough parameters.\n");
            response = "error: invalid # of parameters";
            return; // send error
        }
    }

    Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", slot, host, mode, tmp);

    // Handle DISK slots
    if (slot < MAX_DISK_DEVICES)
    {
        // TODO: Set HOST and MODE
        memcpy(_fnDisks[slot].filename, tmp, MAX_FILENAME_LEN);
        _populate_config_from_slots();
    }
    else
    {
        Debug_println("BAD DEVICE SLOT");
        // Send error
        response = "error: invalid device slot\r";
        return;
    }

    Config.save();
    response = "ok";
}

// Get a 256 byte filename from device slot
void iecFuji::get_device_filename()
{
    Debug_println("Fuji CMD: get device filename");

    uint8_t ds = 0xFF;

    if (payload[0] == FUJICMD_GET_DEVICE_FULLPATH)
        ds = payload[1];
    else
    {
        

        if (pt.size() < 2)
        {
            Debug_printf("Incorrect # of parameters.\n");
            response = "invalid # of parameters";
            // Send error
            return;
        }

        ds = atoi(pt[1].c_str());
    }

    if (!_validate_device_slot(ds, "get_device_filename"))
    {
        Debug_printf("Invalid device slot: %u\n", ds);
        response = "invalid device slot";
        // send error
        return;
    }

    std::string reply = std::string(_fnDisks[ds].filename);
    response = reply;
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

    _populate_slots_from_config();

    IEC.addDevice(new iecDisk(), 8);
    IEC.addDevice(new iecNetwork(), 12);
    IEC.addDevice(new iecCpm(), 14);
    IEC.addDevice(new iecClock(), 29);
    IEC.addDevice(this, 0x0F);
}

iecDisk *iecFuji::bootdisk()
{
    return &_bootDisk;
}

device_state_t iecFuji::process()
{
    virtualDevice::process();

    if (commanddata.channel != CHANNEL_COMMAND)
    {
        Debug_printf("Fuji device only accepts on channel 15. Sending NOTFOUND.\n");
        device_state = DEVICE_ERROR;
        IEC.senderTimeout();
    }

    if (commanddata.primary == IEC_TALK && commanddata.secondary == IEC_REOPEN)
    {
        #ifdef DEBUG
        if (response.size()>0) 
        {  
            Debug_printf("Sending: ");

            // Hex
            for (int i=0;i<response.size();i++)
            {
                Debug_printf("%02X ",response[i]);
            }

            Debug_printf("  ");
            // ASCII Text representation
            for (int i=0;i<response.size();i++)
            {
                char c = petscii2ascii(response[i]);
                Debug_printf("%c", c<0x20 || c>0x7f ? '.' : c);
            }
        }
        
        Debug_printf("\n");
        #endif

        while (!IEC.sendBytes(response))
            ;
    }
    else if (commanddata.primary == IEC_UNLISTEN)
    {
        if (payload[0] > 0x7F)
            process_raw_commands();
        else
            process_basic_commands();
    }

    return device_state;
}

// COMMODORE SPECIFIC CONVENIENCE COMMANDS /////////////////////

void iecFuji::local_ip()
{
    char msg[17];

    fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);

    sprintf(msg, "%u.%u.%u.%u", cfg.localIP[0], cfg.localIP[1], cfg.localIP[2], cfg.localIP[3]);

    iecStatus.channel = 15;
    iecStatus.error = 0;
    iecStatus.msg = string(msg);
    iecStatus.connected = 0;
}

void iecFuji::process_basic_commands()
{
    // Store raw payload before mapping to ASCII, in case it is needed (e.g. storing unmodified appkey data)
    payloadRaw = payload;

    mstr::toASCII(payload);
    pt = tokenize_basic_command(payload);

    if (payload.find("adapterconfig") != std::string::npos)
        get_adapter_config();
    else if (payload.find("setssid") != std::string::npos)
        net_set_ssid();
    else if (payload.find("getssid") != std::string::npos)
        net_get_ssid();
    else if (payload.find("reset") != std::string::npos)
        reset_fujinet();
    else if (payload.find("scanresult") != std::string::npos)
        net_scan_result();
    else if (payload.find("scan") != std::string::npos)
        net_scan_networks();
    else if (payload.find("wifistatus") != std::string::npos)
        net_get_wifi_status();
    else if (payload.find("mounthost") != std::string::npos)
        mount_host();
    else if (payload.find("mountdrive") != std::string::npos)
        disk_image_mount();
    else if (payload.find("opendir") != std::string::npos)
        open_directory();
    else if (payload.find("readdir") != std::string::npos)
        read_directory_entry();
    else if (payload.find("closedir") != std::string::npos)
        close_directory();
    else if (payload.find("gethost") != std::string::npos ||
             payload.find("flh") != std::string::npos)
        read_host_slots();
    else if (payload.find("puthost") != std::string::npos ||
             payload.find("fhost") != std::string::npos)
        write_host_slots();
    else if (payload.find("getdrive") != std::string::npos)
        read_device_slots();
    else if (payload.find("unmounthost") != std::string::npos)
        unmount_host();
    else if (payload.find("getdirpos") != std::string::npos)
        get_directory_position();
    else if (payload.find("setdirpos") != std::string::npos)
        set_directory_position();
    else if (payload.find("setdrivefilename") != std::string::npos)
        set_device_filename();
    else if (payload.find("writeappkey") != std::string::npos)
        write_app_key();
    else if (payload.find("readappkey") != std::string::npos)
        read_app_key();
    else if (payload.find("openappkey") != std::string::npos)
        open_app_key();
    else if (payload.find("closeappkey") != std::string::npos)
        close_app_key();
    else if (payload.find("drivefilename") != std::string::npos)
        get_device_filename();
    else if (payload.find("bootconfig") != std::string::npos)
        set_boot_config();
    else if (payload.find("bootmode") != std::string::npos)
        set_boot_mode();
    else if (payload.find("mountall") != std::string::npos)
        mount_all();
    else if (payload.find("localip") != std::string::npos)
        local_ip();
}

void iecFuji::process_raw_commands()
{
    #ifdef DEBUG
    if (response.size()>0) 
    {  
        int size=payload.size();
        Debug_printf("Received RAW Command: (%i bytes) ", size);
        
        if (size>256)
            size=256;

        // Hex
        for (int i=0;i<size;i++)
        {
            Debug_printf("%02X ",payload[i]);
        }

        Debug_printf("  ");
        // ASCII Text representation
        for (int i=0;i<size;i++)
        {
            char c = petscii2ascii(payload[i]);
            Debug_printf("%c", c<0x20 || c>0x7f ? '.' : c);
        }
        Debug_printf("\n");
    }
    #endif
    
    switch (payload[0])
    {
    case FUJICMD_RESET:
        reset_fujinet();
        break;
    case FUJICMD_GET_SSID:
        net_get_ssid();
        break;
    case FUJICMD_SCAN_NETWORKS:
        net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        net_scan_result();
        break;
    case FUJICMD_SET_SSID:
        net_set_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        mount_host();
        break;
    case FUJICMD_MOUNT_IMAGE:
        disk_image_mount();
        break;
    case FUJICMD_OPEN_DIRECTORY:
        open_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        read_directory_entry();
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        close_directory();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        write_device_slots();
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        // Not implemented.
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        disk_image_umount();
        break;
    case FUJICMD_UNMOUNT_HOST:
        unmount_host();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        get_adapter_config();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        set_directory_position();
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        set_device_filename();
        break;
    case FUJICMD_WRITE_APPKEY:
        write_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        close_app_key();
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        get_device_filename();
        break;
    case 0xD9:
        set_boot_config();
        break;
    case FUJICMD_SET_BOOT_MODE:
        set_boot_mode();
        break;
    case FUJICMD_MOUNT_ALL:
        mount_all();
        break;
    }
}

int iecFuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string iecFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

/* @brief Tokenizes the payload command and parameters.
 Example: "COMMAND:Param1,Param2" will return a vector of [0]="COMMAND", [1]="Param1",[2]="Param2"
 Also supports "COMMAND,Param1,Param2"
*/
vector<string> iecFuji::tokenize_basic_command(string command)
{
    Debug_printf("Tokenizing basic command: %s\n", command.c_str());

    // Replace the first ":" with "," for easy tokenization. 
    // Assume it is fine to change the payload at this point.
    // Technically, "COMMAND,Param1,Param2" will work the smae, if ":" is not in a param value
    size_t endOfCommand = command.find(':');
    if (endOfCommand != std::string::npos)
        command.replace(endOfCommand,1,",");
    
    vector<string> result =  util_tokenize(command, ',');
    return result;

}

#endif /* BUILD_IEC */