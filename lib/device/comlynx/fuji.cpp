#ifdef BUILD_LYNX

#include "fuji.h"

#include <cstring>

#include "../../include/debug.h"

#include "serial.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"

#include "utils.h"
#include "string_utils.h"

#define ADDITIONAL_DETAILS_BYTES 12

#define COPY_SIZE 532

lynxFuji theFuji;        // global fuji device object
lynxNetwork *theNetwork; // global network device object (temporary)
lynxPrinter *thePrinter; // global printer
lynxSerial *theSerial;   // global serial

using namespace std;

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
lynxFuji::lynxFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

// Status
void lynxFuji::comlynx_control_status()
{
    uint8_t r[6] = {0x8F, 0x00, 0x04, 0x00, 0x00, 0x04};
    comlynx_send_buffer(r, 6);
}

// Reset FujiNet
void lynxFuji::comlynx_reset_fujinet()
{
    Debug_println("COMLYNX RESET FUJINET");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }
         
    comlynx_response_ack();
    fnSystem.reboot();
}

// Scan for networks
void lynxFuji::comlynx_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    _countScannedSSIDs = fnWiFi.scan_networks();

    response[0] = _countScannedSSIDs;
    response_len = 1;

    Debug_printf("comlynx_net_scan_networks, _countScannedSSIDs %d\n", _countScannedSSIDs);

    comlynx_response_ack();
}

// Return scanned network entry
void lynxFuji::comlynx_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");
  
    uint8_t n = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    // Response to FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        uint8_t rssi;
    } detail;

    memset(&detail, 0, sizeof(detail));

    if (n < _countScannedSSIDs)
        fnWiFi.get_scan_result(n, detail.ssid, &detail.rssi);

    Debug_printf("SSID: %s - RSSI: %u\n", detail.ssid, detail.rssi);

    memset(response, 0, sizeof(response));
    memcpy(response, &detail, sizeof(detail));
    response_len = sizeof(detail);

    comlynx_response_ack();
}

//  Get SSID
void lynxFuji::comlynx_net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    // Response to FUJICMD_GET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    /*
     We memcpy instead of strcpy because technically the SSID and phasephrase aren't strings and aren't null terminated,
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

    comlynx_response_ack();
}

// Set SSID
void lynxFuji::comlynx_net_set_ssid(uint16_t s)
{
    uint8_t save;

    Debug_println("Fuji cmd: SET SSID");

    save = comlynx_recv();

    // Data for FUJICMD_SET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    s--;
    s--;
    comlynx_recv_buffer((uint8_t *)&cfg, s);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

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

    comlynx_response_ack();
}

// Get WiFi Status
void lynxFuji::comlynx_net_get_wifi_status()
{
    Debug_println("Fuji cmd: GET WIFI STATUS");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    response[0] = wifiStatus;
    response_len = 1;

    comlynx_response_ack();
}

// Mount Server
void lynxFuji::comlynx_mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    if (hostMounted[hostSlot] == false)
    {
        _fnHosts[hostSlot].mount();
        hostMounted[hostSlot] = true;
    }

    comlynx_response_ack();
}

// Mount Server
void lynxFuji::comlynx_unmount_host()
{
    Debug_println("Fuji cmd: UNMOUNT HOST");

    unsigned char hostSlot = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    if (hostMounted[hostSlot] == false)
    {
        _fnHosts[hostSlot].umount();
        hostMounted[hostSlot] = true;
    }

    comlynx_response_ack();
}

// Disk Image Mount
void lynxFuji::comlynx_disk_image_mount()
{
    Debug_println("Fuji cmd: MOUNT IMAGE");

    uint8_t deviceSlot = comlynx_recv();
    uint8_t options = comlynx_recv(); // DISK_ACCESS_MODE

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    // TODO: Implement FETCH?
    char flag[3] = {'r', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    // A couple of reference variables to make things much easier to read...
    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 disk.filename, disk.host_slot, flag, deviceSlot + 1);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled

    boot_config = false;
    disk.disk_dev.is_config_device = false;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
    disk.disk_dev.device_active = true;

    comlynx_response_ack();
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void lynxFuji::comlynx_set_boot_config()
{
    // does nothing on Lynx -SJ
    
    /*
    Debug_printf("Boot config is now %d",boot_config);

    boot_config = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    if (_fnDisks[0].disk_dev.is_config_device)
    {
        _fnDisks[0].disk_dev.unmount();
        _fnDisks[0].disk_dev.is_config_device = false;
        _fnDisks[0].reset();
        Debug_printf("Boot config unmounted slot 0");
    }

    comlynx_response_ack(); */
}

// Do SIO copy
void lynxFuji::comlynx_copy_file()
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
    unsigned long total=0;

    Debug_printf("COMLYNX COPY FILE\n");

    memset(&csBuf, 0, sizeof(csBuf));

    sourceSlot = comlynx_recv();
    destSlot = comlynx_recv();
    comlynx_recv_buffer(csBuf,sizeof(csBuf));

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    dataBuf = (char *)malloc(COPY_SIZE);

    copySpec = string((char *)csBuf);

    Debug_printf("copySpec: %s\n", copySpec.c_str());

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
    destFile = _fnHosts[destSlot].file_open(destPath.c_str(), (char *)destPath.c_str(), destPath.size() + 1, "w");

    size_t count = 0;

    while (!(ferror(sourceFile) || feof(sourceFile)))
    {
        count = fread(dataBuf, 1, COPY_SIZE, sourceFile);
        fwrite(dataBuf, 1, count, destFile);
        total += count;
        Debug_printf("Copied: %lu bytes %u %u\n",total,feof(sourceFile),ferror(sourceFile));
        taskYIELD();
    }

    // copyEnd:
    fclose(sourceFile);
    fclose(destFile);
    free(dataBuf);

    Debug_printf("COPY DONE\n");

    comlynx_response_ack();
}

// Mount all
void lynxFuji::mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    Debug_println("fujinet_mount_all()");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        Debug_printf("mount_all %d '%s' from host #%u as %s on D%u:\n", i, disk.filename, disk.host_slot, flag, i + 1);

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT && strlen(disk.filename) > 0)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                comlynx_response_nack();
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                comlynx_response_nack();
                return;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;

            // We need the file size for loading XEX files and for CASSETTE, so get that too
            disk.disk_size = host.file_size(disk.fileh);

            // And now mount it
            disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
        }
    }

    if (nodisks)
    {
        // No disks in a slot, disable config
        boot_config = false;
    }

    // Go ahead and respond ok
    comlynx_response_ack();
}

// Set boot mode
void lynxFuji::comlynx_set_boot_mode()
{
    // does nothing on Lynx -SJ
    
    /*
    uint8_t bm = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    insert_boot_device(bm);
    boot_config = true;

    comlynx_response_ack();
    */
}

char *_generate_appkey_filename(appkey *info)
{
    static char filenamebuf[30];

    snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key", info->creator, info->app, info->key);
    return filenamebuf;
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void lynxFuji::comlynx_write_app_key()
{
    uint16_t creator = comlynx_recv_length();
    uint8_t app = comlynx_recv();
    uint8_t key = comlynx_recv();
    uint8_t data[64];
    char appkeyfilename[30];
    FILE *fp;

    Debug_printf("Fuji Cmd: WRITE APPKEY %s\n", appkeyfilename);

    snprintf(appkeyfilename, sizeof(appkeyfilename), "/FujiNet/%04hx%02hhx%02hhx.key", creator, app, key);

    comlynx_recv_buffer(data, 64);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    fp = fnSDFAT.file_open(appkeyfilename, "w");
    if (fp == nullptr)
    {
        Debug_printf("Could not open.\n");
        return;
    }

    fwrite(data, sizeof(uint8_t), sizeof(data), fp);
    fclose(fp);

    comlynx_response_ack();
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void lynxFuji::comlynx_read_app_key()
{
    uint16_t creator = comlynx_recv_length();
    uint8_t app = comlynx_recv();
    uint8_t key = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    char appkeyfilename[30];
    FILE *fp;

    snprintf(appkeyfilename, sizeof(appkeyfilename), "/FujiNet/%04hx%02hhx%02hhx.key", creator, app, key);

    fp = fnSDFAT.file_open(appkeyfilename, "r");

    memset(response, 0, sizeof(response));

    if (fp == nullptr)
    {
        Debug_printf("Could not open key.");
        response_len = 1; // if no file found set return length to 1 or lynx hangs waiting for response
        return;
    }
    
    response_len = fread(response, sizeof(char), 64, fp);
    fclose(fp);

    comlynx_response_ack();
}

// DEBUG TAPE
void lynxFuji::debug_tape()
{
}

// Disk Image Unmount
void lynxFuji::comlynx_disk_image_umount()
{
    unsigned char ds = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    _fnDisks[ds].disk_dev.unmount();
    _fnDisks[ds].reset();

    comlynx_response_ack();
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void lynxFuji::image_rotate()
{
    Debug_println("Fuji cmd: IMAGE ROTATE");

    // probably won't be needed on Lynx -SJ    

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
            _comlynx_bus->changeDeviceId(&_fnDisks[n].disk_dev, swap);
        }

        // The first slot gets the device ID of the last slot
        _comlynx_bus->changeDeviceId(&_fnDisks[0].disk_dev, last_id);
    }
}

// This gets called when we're about to shutdown/reboot
void lynxFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

char dirpath[256];

void lynxFuji::comlynx_open_directory(uint16_t s)
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    uint8_t hostSlot = comlynx_recv();

    s--;
    s--;      

    comlynx_recv_buffer((uint8_t *)&dirpath, s);

    Debug_printf("comlynx_open_directory: dirpath: %s\n", dirpath);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    //ComLynx.start_time = esp_timer_get_time();

    if (_current_open_directory_slot == -1)
    {
        // See if there's a search pattern after the directory path
        const char *pattern = nullptr;
        /*int pathlen = strnlen(dirpath, sizeof(dirpath));
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
        */
        Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath, pattern ? pattern : "");

        if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
        {
            _current_open_directory_slot = hostSlot;
        }
        comlynx_response_ack();
    }
    else
    {
        comlynx_response_ack();
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
    if (f->isDir)                       // Also subtract a byte for a terminating slash on directories
        maxlen--;
    if (strlen(f->filename) >= maxlen)
        dest[11] |= FF_TRUNC;

    // File type
    dest[12] = MediaType::discover_mediatype(f->filename);

    Debug_printf("Addtl: ");
    for (int i = 0; i < ADDITIONAL_DETAILS_BYTES; i++)
        Debug_printf("%02x ", dest[i]);
    Debug_printf("\n");
}

void lynxFuji::comlynx_read_directory_entry()
{
    Debug_printf("READ DIR ENTRY\n");
    uint8_t maxlen = comlynx_recv();
    uint8_t addtl = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

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
                filelen = util_ellipsize(f->filename, filenamedest, bufsize - 1);
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
        // I don't think we need any of this -SJ
        /*
        if (maxlen == 38)
        {
            memmove(&dirpath[2], dirpath, 254);

            if (strstr(dirpath, ".ROM") || strstr(dirpath, ".rom"))
            {
                dirpath[0] = 0x89;
                dirpath[1] = 0x8a;
            }
            else if (strstr(dirpath, "/"))
            {
                dirpath[0] = 0x83;
                dirpath[1] = 0x84;
            }
            else
                dirpath[0] = dirpath[1] = 0x20;
        }*/

        memset(response, 0, sizeof(response));
        memcpy(response, dirpath, maxlen);
        response_len = maxlen;
        comlynx_response_ack();
    }
    else
    {
        Debug_printf("Already filled. response is %s\n",response);
        //ComLynx.start_time = esp_timer_get_time();
        comlynx_response_ack();
    }
}

void lynxFuji::comlynx_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    response_len = sizeof(pos);
    memcpy(response, &pos, sizeof(pos));

    comlynx_response_ack();
}

void lynxFuji::comlynx_set_directory_position()
{
    uint16_t pos = 0;

    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    comlynx_recv_buffer((uint8_t *)&pos, sizeof(uint16_t));

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    _fnHosts[_current_open_directory_slot].dir_seek(pos);
    comlynx_response_ack();
    Debug_printf("pos is now %u", pos);
}

void lynxFuji::comlynx_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    response_len = 1;
    comlynx_response_ack();
}

// Get network adapter configuration
void lynxFuji::comlynx_get_adapter_config()
{
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    // Response to FUJICMD_GET_ADAPTERCONFIG
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

    comlynx_response_ack();
}

//  Make new disk and shove into device slot
void lynxFuji::comlynx_new_disk()
{
    uint8_t hs = comlynx_recv();
    uint8_t ds = comlynx_recv();
    uint32_t numBlocks;
    uint8_t *c = (uint8_t *)&numBlocks;
    uint8_t p[256];

    comlynx_recv_buffer(c, sizeof(uint32_t));
    comlynx_recv_buffer(p, 256);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (host.file_exists((const char *)p))
    {
        //ComLynx.start_time = esp_timer_get_time();
        comlynx_response_ack();
        return;
    }
    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *)p, 256);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");

    Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

    disk.disk_dev.write_blank(disk.fileh, numBlocks);

    fclose(disk.fileh);
    comlynx_response_ack();
}

// Send host slot data to computer
void lynxFuji::comlynx_read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    memcpy(response, hostSlots, sizeof(hostSlots));
    response_len = sizeof(hostSlots);

    comlynx_response_ack();
}

// Read and save host slot data from computer
void lynxFuji::comlynx_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    comlynx_recv_buffer((uint8_t *)hostSlots, sizeof(hostSlots));

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    for (int i = 0; i < MAX_HOSTS; i++)
    {
        hostMounted[i] = false;
        _fnHosts[i].set_hostname(hostSlots[i]);
    }
    _populate_config_from_slots();
    Config.save();

    comlynx_response_ack();
}

// Store host path prefix
void lynxFuji::comlynx_set_host_prefix()
{
}

// Retrieve host path prefix
void lynxFuji::comlynx_get_host_prefix()
{
}

// Send device slot data to computer
void lynxFuji::comlynx_read_device_slots()
{
    Debug_println("Fuji cmd: READ DEVICE SLOTS");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

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

    memcpy(response, &diskSlots, returnsize);
    response_len = returnsize;

    comlynx_response_ack();
}

// Read and save disk slot data from computer
void lynxFuji::comlynx_write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    struct
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    } diskSlots[MAX_DISK_DEVICES];

    comlynx_recv_buffer((uint8_t *)&diskSlots, sizeof(diskSlots));

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    //Debug_printf("comlnyx_write_device_slots, hs:%d m:%d %s\n", diskSlots[0].hostSlot, diskSlots[0].mode, diskSlots[0].filename);

    // Load the data into our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

    // Save the data to disk
    _populate_config_from_slots();
    Config.mark_dirty();                             // not sure why, but I have to mark as dirty
    Config.save();

    comlynx_response_ack();
}

// Temporary(?) function while we move from old config storage to new
void lynxFuji::_populate_slots_from_config()
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
void lynxFuji::_populate_config_from_slots()
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
void lynxFuji::comlynx_set_device_filename(uint16_t s)
{
    unsigned char ds = comlynx_recv();
    s--;
    s--;

    Debug_printf("SET DEVICE SLOT %d filename\n", ds);

    comlynx_recv_buffer((uint8_t *)&f, s);

    Debug_printf("filename: %s\n", f);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    memcpy(_fnDisks[ds].filename, f, MAX_FILENAME_LEN);
    _populate_config_from_slots();

    comlynx_response_ack();
}

// Get a 256 byte filename from device slot
void lynxFuji::comlynx_get_device_filename()
{
    unsigned char ds = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    memcpy(response, _fnDisks[ds].filename, 256);
    response_len = 256;

    comlynx_response_ack();
}

// Mounts the desired boot disk number
void lynxFuji::insert_boot_device(uint8_t d)
{
    // This isn't needed on Lynx -SJ
    
    /*
    // TODO: Change this when CONFIG is ready.
    const char *config_atr = "/autorun.ddp";
    const char *mount_all_atr = "/mount-and-boot.ddp";
    FILE *fBoot;

    switch (d)
    {
    case 0:
        fBoot = fsFlash.file_open(config_atr);
        _fnDisks[0].disk_dev.mount(fBoot, config_atr, 262144, MEDIATYPE_DDP);
        break;
    case 1:

        fBoot = fsFlash.file_open(mount_all_atr);
        _fnDisks[0].disk_dev.mount(fBoot, mount_all_atr, 262144, MEDIATYPE_DDP);
        break;
    }

    _fnDisks[0].disk_dev.is_config_device = true;
    _fnDisks[0].disk_dev.device_active = true;
    */
}

void lynxFuji::comlynx_enable_device()
{
    unsigned char d = comlynx_recv();
    Debug_printf("FUJI ENABLE DEVICE %02x\n",d);
    
    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    switch(d)
    {
        case 0x02:
            Config.store_printer_enabled(true);
            break;
        case 0x04:
            Config.store_device_slot_enable_1(true);
            break;
        case 0x05:
            Config.store_device_slot_enable_2(true);
            break;
        case 0x06:
            Config.store_device_slot_enable_3(true);
            break;
        case 0x07:
            Config.store_device_slot_enable_4(true);
            break;
    }

    Config.save();
    ComLynx.enableDevice(d);
    comlynx_response_ack();
}

void lynxFuji::comlynx_disable_device()
{
    unsigned char d = comlynx_recv();
    Debug_printf("FUJI DISABLE DEVICE %02x\n",d);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    switch(d)
    {
        case 0x02:
            Config.store_printer_enabled(false);
            break;
        case 0x04:
            Config.store_device_slot_enable_1(false);
            break;
        case 0x05:
            Config.store_device_slot_enable_2(false);
            break;
        case 0x06:
            Config.store_device_slot_enable_3(false);
            break;
        case 0x07:
            Config.store_device_slot_enable_4(false);
            break;
    }

    Config.save();    
    ComLynx.disableDevice(d);
    comlynx_response_ack();
}

// Initializes base settings and adds our devices to the SIO bus
void lynxFuji::setup(systemBus *siobus)
{
    // set up Fuji device
    _comlynx_bus = siobus;

    _populate_slots_from_config();

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = false;

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = false;
    _comlynx_bus->addDevice(&_fnDisks[0].disk_dev, 4);
    _comlynx_bus->addDevice(&theFuji, 0x0F);   // Fuji becomes the gateway device.
    theNetwork = new lynxNetwork();
    _comlynx_bus->addDevice(theNetwork, 0x09); // temporary.
}

void lynxFuji::comlynx_random_number()
{
    int *p = (int *)&response[0];

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    response_len = sizeof(int);
    *p = rand();
    
    comlynx_response_ack();
}

void lynxFuji::comlynx_get_time()
{
    Debug_println("FUJI GET TIME");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    time_t tt = time(nullptr);

    setenv("TZ",Config.get_general_timezone().c_str(),1);
    tzset();

    struct tm * now = localtime(&tt);

    now->tm_mon++;
    now->tm_year-=100;

    response[0] = now->tm_mday;
    response[1] = now->tm_mon;
    response[2] = now->tm_year;
    response[3] = now->tm_hour;
    response[4] = now->tm_min;
    response[5] = now->tm_sec;

    response_len = 6;

    Debug_printf("Sending %02X %02X %02X %02X %02X %02X\n",now->tm_mday, now->tm_mon, now->tm_year, now->tm_hour, now->tm_min, now->tm_sec);
    comlynx_response_ack();
}

void lynxFuji::comlynx_device_enable_status()
{
    uint8_t d = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    if (ComLynx.deviceExists(d))
        comlynx_response_ack();
    else
        comlynx_response_nack();

    response_len=1;
    response[0]=ComLynx.deviceEnabled(d);
}

lynxDisk *lynxFuji::bootdisk()
{
    return _bootDisk;
}


fujiHost *lynxFuji::set_slot_hostname(int host_slot, char *hostname)
{

    _fnHosts[host_slot].set_hostname(hostname);
    _populate_config_from_slots();

    return &_fnHosts[host_slot];
}


void lynxFuji::comlynx_hello()
{
    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    const char resp[] = "HI FROM FUJINET!\n";
    response_len = strlen(resp);
    memcpy(response,resp,response_len);

    Debug_printf("lynxFuji::comlynx_hello()\n");
    comlynx_response_ack();

    Debug_printf("HELLO FROM LYNX.\n");

}

// Set UDP Stream HOST & PORT and start it
void lynxFuji::comlynx_enable_udpstream(uint16_t s)
{
    char host[128];
    
    s--;

    // Receive port #
    unsigned short port;

    port  = comlynx_recv() & 0xFF;
    s--;
    port |= comlynx_recv() << 8;
    s--;

    Debug_printf("comlynx_enable_udpstream(); p=%u - s=%u",port,s);

    // Receive host
    comlynx_recv_buffer((uint8_t *)host,s);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    // Acknowledge
    comlynx_response_ack();

    // Save the host and port
    Config.store_udpstream_host(host);
    Config.store_udpstream_port(port);
    Config.save();

    // Start the UDP Stream
    ComLynx.setUDPHost(host, port);
}

void lynxFuji::comlynx_control_send()
{
    // Reset the recvbuffer
    recvbuffer_len = 0;         // happens in recv_length, but may remove from there -SJ
    
    uint16_t s = comlynx_recv_length();
    uint8_t c = comlynx_recv();

    switch (c)
    {
    case FUJICMD_RESET:
        comlynx_reset_fujinet();
        break;
    case FUJICMD_GET_SSID:
        comlynx_net_get_ssid();
        break;
    case FUJICMD_SCAN_NETWORKS:
        comlynx_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        comlynx_net_scan_result();
        break;
    case FUJICMD_SET_SSID:
        comlynx_net_set_ssid(s);
        break;
    case FUJICMD_GET_WIFISTATUS:
        comlynx_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        comlynx_mount_host();
        break;
    case FUJICMD_UNMOUNT_HOST:
        comlynx_unmount_host();
        break;
    case FUJICMD_MOUNT_IMAGE:
        comlynx_disk_image_mount();
        break;
    case FUJICMD_OPEN_DIRECTORY:
        comlynx_open_directory(s);
        break;
    case FUJICMD_READ_DIR_ENTRY:
        comlynx_read_directory_entry();
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        comlynx_close_directory();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        comlynx_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        comlynx_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        comlynx_read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        comlynx_write_device_slots();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        comlynx_disk_image_umount();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        comlynx_get_adapter_config();
        break;
    case FUJICMD_NEW_DISK:
        comlynx_new_disk();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        comlynx_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        comlynx_set_directory_position();
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        comlynx_set_device_filename(s);
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        comlynx_get_device_filename();
        break;
    case FUJICMD_CONFIG_BOOT:
        comlynx_set_boot_config();
        break;
    case FUJICMD_ENABLE_DEVICE:
        comlynx_enable_device();
        break;
    case FUJICMD_DISABLE_DEVICE:
        comlynx_disable_device();
        break;
    case FUJICMD_MOUNT_ALL:
        mount_all();
        break;
    case FUJICMD_SET_BOOT_MODE:
        comlynx_set_boot_mode();
        break;
    case FUJICMD_WRITE_APPKEY:
        comlynx_write_app_key();
        break;
    case FUJICMD_READ_APPKEY:
        comlynx_read_app_key();
        break;
    case FUJICMD_RANDOM_NUMBER:
        comlynx_random_number();
        break;
    case FUJICMD_GET_TIME:
        comlynx_get_time();
        break;
    case FUJICMD_DEVICE_ENABLE_STATUS:
        comlynx_device_enable_status();
        break;
    case FUJICMD_COPY_FILE:
        comlynx_copy_file();
        break;
    case FUJICMD_ENABLE_UDPSTREAM:
        comlynx_enable_udpstream(s);
        break;
    case 0x01:
        comlynx_hello();
        break;
    }
}

void lynxFuji::comlynx_control_clr()
{
    uint8_t b; 
    
    comlynx_send(0xBF);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(comlynx_checksum(response, response_len));
    b = comlynx_recv();             // get the ack or nack
    // ignore response from Lynx, if they didn't receive the data properly
    // they should resend the entire command -SJ
    
    Debug_printf("comlynx_control_clr: %02X\n", b);
    
    // Reset response buffer    
    memset(response, 0, sizeof(response));
    response_len = 0;
}

void lynxFuji::comlynx_process(uint8_t b)
{
    unsigned char c = b >> 4;
    //Debug_printf("%02x \n",c);
    
    switch (c)
    {
    case MN_STATUS:
        comlynx_control_status();
        break;
    case MN_CLR:
        comlynx_control_clr();
        break;
    case MN_RECEIVE:
        comlynx_response_ack();
        break;
    case MN_SEND:
        comlynx_control_send();
        break;
    case MN_READY:
        comlynx_control_ready();
        break;
    }
}

int lynxFuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string lynxFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

#endif /* BUILD_LYNX */
