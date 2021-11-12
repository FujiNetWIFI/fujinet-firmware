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

#define ADDITIONAL_DETAILS_BYTES 12

adamFuji theFuji;        // global fuji device object
adamNetwork *theNetwork; // global network device object (temporary)

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
adamFuji::adamFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void adamFuji::adamnet_control_status()
{
    uint8_t r[6] = {0x8F, 0x00, 0x04, 0x00, 0x00, 0x04};
    adamnet_send_buffer(r, 6);
}

// Reset FujiNet
void adamFuji::adamnet_reset_fujinet()
{
    adamnet_recv(); // get ck
    Debug_println("ADAMNET RESET FUJINET");
    fnSystem.delay_microseconds(80);
    adamnet_send(0x9F); // ACK
    fnSystem.reboot();
}

// Scan for networks
void adamFuji::adamnet_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    adamnet_recv(); // get ck

    isReady = false;

    if (scanStarted == false)
    {
        _countScannedSSIDs = fnWiFi.scan_networks();
        scanStarted = true;
        setSSIDStarted = false;
    }

    isReady = true;

    fnSystem.delay_microseconds(80);

    response[0] = _countScannedSSIDs;
    response_len = 1;

    adamnet_send(0x9F); // ACK
}

// Return scanned network entry
void adamFuji::adamnet_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");
    scanStarted = false;

    uint8_t n = adamnet_recv();

    adamnet_recv(); // get CK

    // Response to SIO_FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN];
        uint8_t rssi;
    } detail;

    memset(&detail, 0, sizeof(detail));

    bool err = false;
    if (n < _countScannedSSIDs)
        fnWiFi.get_scan_result(n, detail.ssid, &detail.rssi);
    else
    {
        err = true;
    }

    Debug_printf("SSID: %s - RSSI: %u\n", detail.ssid, detail.rssi);

    memset(response, 0, sizeof(response));
    memcpy(response, &detail, sizeof(detail));
    response_len = 33;
    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F); // ACK.
}

//  Get SSID
void adamFuji::adamnet_net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    adamnet_recv(); // get CK

    // Response to SIO_FUJICMD_GET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN];
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

    // Move into response.
    memcpy(response, &cfg, sizeof(cfg));
    response_len = sizeof(cfg);

    fnSystem.delay_microseconds(150);

    adamnet_send(0x9F); // ACK
}

// Set SSID
void adamFuji::adamnet_net_set_ssid(uint16_t s)
{
    if (!fnWiFi.connected() && setSSIDStarted == false)
    {
        Debug_println("Fuji cmd: SET SSID");

        s--;

        // Data for SIO_FUJICMD_SET_SSID
        struct
        {
            char ssid[MAX_SSID_LEN];
            char password[MAX_WIFI_PASS_LEN];
        } cfg;

        adamnet_recv_buffer((uint8_t *)&cfg, s);

        uint8_t ck = adamnet_recv();

        fnSystem.delay_microseconds(100);
        adamnet_send(0x9F); // ACK
        bool save = true;

        Debug_printf("Connecting to net: %s password: %s\n", cfg.ssid, cfg.password);

        fnWiFi.connect(cfg.ssid, cfg.password);
        setSSIDStarted = true;
        // Only save these if we're asked to, otherwise assume it was a test for connectivity
        if (save)
        {
            Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
            Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
            Config.save();
        }
    }
}
// Get WiFi Status
void adamFuji::adamnet_net_get_wifi_status()
{
    adamnet_recv(); // Get CK
    Debug_println("Fuji cmd: GET WIFI STATUS");
    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    response[0] = wifiStatus;
    response_len = 1;
    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F); // ACK
}

// Mount Server
void adamFuji::adamnet_mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = adamnet_recv();

    adamnet_recv(); // Get CK

    if (hostMounted[hostSlot] == false)
    {
        _fnHosts[hostSlot].mount();
        hostMounted[hostSlot] = true;
    }

    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F);
}

// Disk Image Mount
void adamFuji::adamnet_disk_image_mount()
{
    Debug_println("Fuji cmd: MOUNT IMAGE");

    uint8_t deviceSlot = adamnet_recv();
    uint8_t options = adamnet_recv(); // DISK_ACCESS_MODE

    adamnet_recv(); // CK

    // TODO: Implement FETCH?
    char flag[3] = {'r', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 disk.filename, disk.host_slot, flag, deviceSlot + 1);

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F); // ACK

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void adamFuji::adamnet_set_boot_config()
{
    boot_config = adamnet_recv();
    adamnet_recv();

    fnSystem.delay_microseconds(120);
    adamnet_send(0x9F); // ACK
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
    unsigned char ds = adamnet_recv();
    adamnet_recv();

    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F);

    _fnDisks[ds].disk_dev.unmount();
    _fnDisks[ds].reset();
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
    }
}

// This gets called when we're about to shutdown/reboot
void adamFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();

}

char dirpath[256];

void adamFuji::adamnet_open_directory(uint16_t s)
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    uint8_t hostSlot = adamnet_recv();

    s--;
    s--;

    adamnet_recv_buffer((uint8_t *)&dirpath, s);

    adamnet_recv(); // Grab checksum

    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F); // ACK

    if (_current_open_directory_slot == -1)
    {
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
    }

    response_len = 1;
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);
    modtime->tm_mon++;
    modtime->tm_year -= 100;

    dest[0] = modtime->tm_year;
    dest[1] = modtime->tm_mon;
    dest[2] = modtime->tm_mday;
    dest[3] = modtime->tm_hour;
    dest[4] = modtime->tm_min;
    dest[5] = modtime->tm_sec;

    // File size
    uint32_t fsize = f->size;
    dest[6] = fsize & 0xFF;
    dest[7] = (fsize >> 8) & 0xFF;
    dest[8] = (fsize >> 16) & 0xFF;
    dest[9] = (fsize >> 24) & 0xFF;

    // File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

    dest[10] = f->isDir ? FF_DIR : 0;

    maxlen -= ADDITIONAL_DETAILS_BYTES; // Adjust the max return value with the number of additional bytes we're copying
    if (f->isDir) // Also subtract a byte for a terminating slash on directories
        maxlen--;
    if (strlen(f->filename) >= maxlen)
        dest[11] |= FF_TRUNC;

    // File type
    dest[12] = MediaType::discover_mediatype(f->filename);

    Debug_printf("Addtl: ");
    for (int i=0;i<ADDITIONAL_DETAILS_BYTES;i++)
        Debug_printf("%02x ",dest[i]);
    Debug_printf("\n");
}

void adamFuji::adamnet_read_directory_entry()
{
    uint8_t maxlen = adamnet_recv();
    uint8_t addtl = adamnet_recv();

    adamnet_recv(); // Checksum

    if (response[0] == 0x00)
    {
        Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

        fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();

        if (f == nullptr)
        {
            Debug_println("Reached end of of directory");
            dirpath[0] = 0x7F;
            dirpath[1] = 0x7F;
        }
        else
        {
            Debug_printf("::read_direntry \"%s\"\n", f->filename);

            int bufsize = sizeof(dirpath);
            char *filenamedest = dirpath;

            // If 0x80 is set on AUX2, send back additional information
            if (addtl & 0x80)
            {
                _set_additional_direntry_details(f, (uint8_t *)dirpath, maxlen);
                // Adjust remaining size of buffer and file path destination
                bufsize = sizeof(dirpath) - ADDITIONAL_DETAILS_BYTES;
                filenamedest = dirpath + ADDITIONAL_DETAILS_BYTES;
            }
            else
            {
                bufsize = maxlen;
            }

            int filelen;
            // int filelen = strlcpy(filenamedest, f->filename, bufsize);
            if (maxlen < 128)
            {
                filelen = util_ellipsize(f->filename, filenamedest, bufsize);
            }
            else
            {
                filelen = strlcpy(filenamedest, f->filename, bufsize);
            }

            // Add a slash at the end of directory entries
            if (f->isDir && filelen < (bufsize - 2))
            {
                dirpath[filelen] = '/';
                dirpath[filelen + 1] = '\0';
            }
        }

        // Hack-o-rama to add file type character to beginning of path.
        if (maxlen == 32)
        {
            memmove(&dirpath[2], dirpath, 254);
            if (strstr(dirpath, ".DDP") || strstr(dirpath, ".ddp"))
                dirpath[0] = 0x85;
            else if (strstr(dirpath, ".DSK") || strstr(dirpath, ".dsk"))
                dirpath[0] = 0x86;
            else if (strstr(dirpath, ".ROM") || strstr(dirpath, ".rom"))
                dirpath[0] = 0x87;
            else if (strstr(dirpath, "/"))
                dirpath[0] = 0x83;
            else
                dirpath[0] = 0x20;

            dirpath[1] = 0x20;
        }

        memset(response, 0, sizeof(response));
        memcpy(response, dirpath, maxlen);
        response_len = maxlen;
    }

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F); // ACK
}

void adamFuji::adamnet_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();

    adamnet_recv(); // ck

    response_len = sizeof(pos);
    memcpy(response, &pos, sizeof(pos));

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F); // ACK
}

void adamFuji::adamnet_set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // DAUX1 and DAUX2 hold the position to seek to in low/high order
    uint16_t pos = 0;

    adamnet_recv_buffer((uint8_t *)&pos, sizeof(uint16_t));

    Debug_printf("pos is now %u",pos);

    adamnet_recv(); // ck

    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F); // ACK

    _fnHosts[_current_open_directory_slot].dir_seek(pos);
}

void adamFuji::adamnet_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    adamnet_recv(); // ck

    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F); // ACK

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    response_len = 1;
}

// Get network adapter configuration
void adamFuji::adamnet_get_adapter_config()
{
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    adamnet_recv(); // ck

    // Response to SIO_FUJICMD_GET_ADAPTERCONFIG
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

    memcpy(response, &cfg, sizeof(cfg));
    response_len = sizeof(cfg);

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F); // ACK
}

//  Make new disk and shove into device slot
void adamFuji::adamnet_new_disk()
{
}

// Send host slot data to computer
void adamFuji::adamnet_read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    adamnet_recv(); // ck

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    memcpy(response, hostSlots, sizeof(hostSlots));
    response_len = sizeof(hostSlots);

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F);
}

// Read and save host slot data from computer
void adamFuji::adamnet_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    adamnet_recv_buffer((uint8_t *)hostSlots, sizeof(hostSlots));

    adamnet_recv(); // ck

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F); // ACK

    for (int i = 0; i < MAX_HOSTS; i++)
    {
        hostMounted[i] = false;
        _fnHosts[i].set_hostname(hostSlots[i]);
    }
    _populate_config_from_slots();
    Config.save();
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
    Debug_println("Fuji cmd: READ DEVICE SLOTS");

    struct disk_slot
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    };
    disk_slot diskSlots[MAX_DISK_DEVICES];

    memset(&diskSlots, 0, sizeof(diskSlots));

    int returnsize;

    // Load the data from our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        diskSlots[i].mode = _fnDisks[i].access_mode;
        diskSlots[i].hostSlot = _fnDisks[i].host_slot;
        strlcpy(diskSlots[i].filename, _fnDisks[i].filename, MAX_DISPLAY_FILENAME_LEN);
    }

    returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;

    adamnet_recv(); // ck

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F); // ACK

    memcpy(response, &diskSlots, returnsize);
    response_len = returnsize;
}

// Read and save disk slot data from computer
void adamFuji::adamnet_write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    struct
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    } diskSlots[MAX_DISK_DEVICES];

    adamnet_recv_buffer((uint8_t *)&diskSlots, sizeof(diskSlots));

    adamnet_recv(); // ck

    fnSystem.delay_microseconds(100);
    adamnet_send(0x9F); // ACK

    // Load the data into our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

    // Save the data to disk
    _populate_config_from_slots();
    Config.save();
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

char f[MAX_FILENAME_LEN];

// Write a 256 byte filename to the device slot
void adamFuji::adamnet_set_device_filename(uint16_t s)
{
    unsigned char ds = adamnet_recv();
    s--; s--;

    Debug_printf("SET DEVICE SLOT %d filename\n",ds);

    adamnet_recv_buffer((uint8_t *)&f, s);

    Debug_printf("filename: %s\n",f);

    adamnet_recv(); // CK
    adamnet_send(0x9F); // ACK

    memcpy(_fnDisks[ds].filename, f, MAX_FILENAME_LEN);
    _populate_config_from_slots();
}

// Get a 256 byte filename from device slot
void adamFuji::adamnet_get_device_filename()
{
    unsigned char ds = adamnet_recv();

    adamnet_recv();

    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F);

    memcpy(response, _fnDisks[ds].filename, 256);
    response_len = 256;
}

// Mounts the desired boot disk number
void adamFuji::insert_boot_device(uint8_t d)
{
    const char *config_atr = "/autorun.ddp";
    const char *mount_all_atr = "/mount-and-boot.ddp";
    FILE *fBoot;

    switch (d)
    {
    case 0:
        fBoot = fnSPIFFS.file_open(config_atr);
        _bootDisk->mount(fBoot, config_atr, 0);
        break;
    case 1:
        fBoot = fnSPIFFS.file_open(mount_all_atr);
        _bootDisk->mount(fBoot, mount_all_atr, 0);
        break;
    }

    _bootDisk->is_config_device = true;
    _bootDisk->device_active = true;
    Debug_printf("Media type is %d\n",_bootDisk->mediatype());
}

// Initializes base settings and adds our devices to the SIO bus
void adamFuji::setup(adamNetBus *siobus)
{
    // set up Fuji device
    _adamnet_bus = siobus;

    _populate_slots_from_config();

    // insert_boot_device(0);

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = false;

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = false;

    _adamnet_bus->addDevice(&_fnDisks[0].disk_dev, ADAMNET_DEVICEID_DISK);
   
    FILE *f = fnSPIFFS.file_open("/autorun.ddp");
    _fnDisks[0].disk_dev.mount(f,"/autorun.ddp",262144,MEDIATYPE_DDP);

    theNetwork = new adamNetwork();
    _adamnet_bus->addDevice(theNetwork, 0x0E); // temporary.
    _adamnet_bus->addDevice(&theFuji, 0x0F);   // Fuji becomes the gateway device.

    // Add our devices to the AdamNet bus
    //for (int i = 0; i < 4; i++)
    //    _adamnet_bus->addDevice(&_fnDisks[i].disk_dev, ADAMNET_DEVICEID_DISK + i);

    //for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
    //    _adamnet_bus->addDevice(&sioNetDevs[i], ADAMNET_DEVICEID_FN_NETWORK + i);
}

adamDisk *adamFuji::bootdisk()
{
    return _bootDisk;
}

void adamFuji::adamnet_control_ready()
{
    Debug_println("READY - SEND ACK.");
    fnSystem.delay_microseconds(150);
    adamnet_send(0x9F); // ACK.
}

void adamFuji::adamnet_control_send()
{
    uint16_t s = adamnet_recv_length();
    uint8_t c = adamnet_recv();

    switch (c)
    {
    case SIO_FUJICMD_RESET:
        adamnet_reset_fujinet();
        break;
    case SIO_FUJICMD_GET_SSID:
        adamnet_net_get_ssid();
        break;
    case SIO_FUJICMD_SCAN_NETWORKS:
        adamnet_net_scan_networks();
        break;
    case SIO_FUJICMD_GET_SCAN_RESULT:
        adamnet_net_scan_result();
        break;
    case SIO_FUJICMD_SET_SSID:
        adamnet_net_set_ssid(s);
        break;
    case SIO_FUJICMD_GET_WIFISTATUS:
        adamnet_net_get_wifi_status();
        break;
    case SIO_FUJICMD_MOUNT_HOST:
        adamnet_mount_host();
        break;
    case SIO_FUJICMD_MOUNT_IMAGE:
        adamnet_disk_image_mount();
        break;
    case SIO_FUJICMD_OPEN_DIRECTORY:
        adamnet_open_directory(s);
        break;
    case SIO_FUJICMD_READ_DIR_ENTRY:
        adamnet_read_directory_entry();
        break;
    case SIO_FUJICMD_CLOSE_DIRECTORY:
        adamnet_close_directory();
        break;
    case SIO_FUJICMD_READ_HOST_SLOTS:
        adamnet_read_host_slots();
        break;
    case SIO_FUJICMD_WRITE_HOST_SLOTS:
        adamnet_write_host_slots();
        break;
    case SIO_FUJICMD_READ_DEVICE_SLOTS:
        adamnet_read_device_slots();
        break;
    case SIO_FUJICMD_WRITE_DEVICE_SLOTS:
        adamnet_write_device_slots();
        break;
    case SIO_FUJICMD_UNMOUNT_IMAGE:
        adamnet_disk_image_umount();
        break;
    case SIO_FUJICMD_GET_ADAPTERCONFIG:
        adamnet_get_adapter_config();
        break;
    case SIO_FUJICMD_GET_DIRECTORY_POSITION:
        adamnet_get_directory_position();
        break;
    case SIO_FUJICMD_SET_DIRECTORY_POSITION:
        adamnet_set_directory_position();
        break;
    case SIO_FUJICMD_SET_DEVICE_FULLPATH:
        adamnet_set_device_filename(s);
        break;
    case SIO_FUJICMD_GET_DEVICE_FULLPATH:
        adamnet_get_device_filename();
        break;
    case SIO_FUJICMD_CONFIG_BOOT:
        adamnet_set_boot_config();
        break;
    }
}

void adamFuji::adamnet_control_clr()
{
    adamnet_send(0xBF);
    adamnet_send_length(response_len);
    adamnet_send_buffer(response, response_len);
    adamnet_send(adamnet_checksum(response, response_len));
    adamnet_recv(); // get the ack.
    memset(response, 0, sizeof(response));
    response_len = 0;
}

void adamFuji::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_RECEIVE:
        fnSystem.delay_microseconds(80);
        adamnet_send(0x9F); // ACK.
        break;
    case MN_SEND:
        adamnet_control_send();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
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