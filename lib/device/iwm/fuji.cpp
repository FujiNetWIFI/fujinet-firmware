#ifdef BUILD_APPLE
#include "fuji.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "led.h"
#include "fnWiFi.h"

#define IWM_CTRL_RESET 0x00


#define IWM_FUJICMD_RESET 0xFF
#define IWM_FUJICMD_GET_SSID 0xFE
#define IWM_FUJICMD_SCAN_NETWORKS 0xFD
#define IWM_FUJICMD_GET_SCAN_RESULT 0xFC
#define IWM_FUJICMD_SET_SSID 0xFB
#define IWM_FUJICMD_GET_WIFISTATUS 0xFA
#define IWM_FUJICMD_MOUNT_HOST 0xF9
#define IWM_FUJICMD_MOUNT_IMAGE 0xF8
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
#define IWM_FUJICMD_WRITE_APPKEY 0xDE
#define IWM_FUJICMD_READ_APPKEY 0xDD
#define SIO_FUJICMD_OPEN_APPKEY 0xDC
#define SIO_FUJICMD_CLOSE_APPKEY 0xDB
#define SIO_FUJICMD_GET_DEVICE_FULLPATH 0xDA
#define IWM_FUJICMD_CONFIG_BOOT 0xD9
#define IWM_FUJICMD_COPY_FILE 0xD8
#define IWM_FUJICMD_MOUNT_ALL 0xD7
#define IWM_FUJICMD_SET_BOOT_MODE 0xD6
#define SIO_FUJICMD_ENABLE_DEVICE 0xD5
#define SIO_FUJICMD_DISABLE_DEVICE 0xD4
#define SIO_FUJICMD_STATUS 0x53
#define SIO_FUJICMD_HSIO_INDEX 0x3F

iwmFuji theFuji; // Global fuji object.

iwmFuji::iwmFuji()
{
  Debug_printf("Announcing the iwmFuji::iwmFuji()!!!\n");
}

void iwmFuji::iwm_dummy_command() // SP CTRL command
{
  Debug_printf("\r\nData Received: ");
  for (int i=0; i<num_decoded; i++)
    Debug_printf(" %02x", packet_buffer[i]);
}

void iwmFuji::iwm_reset_fujinet() // SP CTRL command
{
    Debug_printf("\r\nFuji cmd: REBOOT");
    encode_status_reply_packet();
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
    // save device unit SP address somewhere and restore it after reboot?
    fnSystem.reboot();
}

void iwmFuji::iwm_net_get_ssid() // SP STATUS command
{
   Debug_println("Fuji cmd: GET SSID");

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
    memcpy(packet_buffer, &cfg, sizeof(cfg));
    packet_len = sizeof(cfg);
} // 0xFE

void iwmFuji::iwm_net_scan_networks() // SP STATUS command 
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    isReady = false;

    if (scanStarted == false)
    {
        _countScannedSSIDs = fnWiFi.scan_networks();
        scanStarted = true;
        setSSIDStarted = false;
    }

    isReady = true;

    packet_buffer[0] = _countScannedSSIDs;
    response_len = 1;
} // 0xFD

void iwmFuji::iwm_net_scan_result() //SP STATUS command 
{
    Debug_println("Fuji cmd: GET SCAN RESULT");
    scanStarted = false;

    uint8_t n = packet_buffer[0]; // pretty sure this is right

    // Response to SIO_FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN];
        uint8_t rssi;
    } detail;

    memset(&detail, 0, sizeof(detail));

    if (n < _countScannedSSIDs)
        fnWiFi.get_scan_result(n, detail.ssid, &detail.rssi);

    Debug_printf("SSID: %s - RSSI: %u\n", detail.ssid, detail.rssi);

    memset(packet_buffer, 0, sizeof(packet_buffer));
    memcpy(packet_buffer, &detail, sizeof(detail));
    packet_len = 33;
} // 0xFC

void iwmFuji::iwm_net_set_ssid() // SP CTRL command
{
 if (!fnWiFi.connected() && setSSIDStarted == false)
    {
        Debug_println("Fuji cmd: SET SSID");

        uint16_t s = packet_buffer[0]; // to do - verify
        s--;

        // Data for SIO_FUJICMD_SET_SSID
        struct
        {
            char ssid[MAX_SSID_LEN];
            char password[MAX_WIFI_PASS_LEN];
        } cfg;

        // to do - copy data over to cfg
        memcpy((uint8_t *)&cfg, (uint8_t *)packet_buffer[1], s);
        // adamnet_recv_buffer((uint8_t *)&cfg, s);

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
} // 0xFB

// Get WiFi Status
void iwmFuji::iwm_net_get_wifi_status() // SP Status command
{
    Debug_println("Fuji cmd: GET WIFI STATUS");
    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    packet_buffer[0] = wifiStatus;
    packet_len = 1;
}

// Mount Server
void iwmFuji::iwm_mount_host() // SP CTRL command
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = packet_buffer[0]; // adamnet_recv();

    if (hostMounted[hostSlot] == false)
    {
        _fnHosts[hostSlot].mount();
        hostMounted[hostSlot] = true;
    }
}

// Disk Image Mount
void iwmFuji::iwm_disk_image_mount() // SP CTRL command
{
    Debug_println("Fuji cmd: MOUNT IMAGE");

    uint8_t deviceSlot = packet_buffer[0]; //adamnet_recv();
    uint8_t options = packet_buffer[1]; //adamnet_recv(); // DISK_ACCESS_MODE

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

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
}


// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void iwmFuji::iwm_set_boot_config() // SP CTRL command
{
    boot_config = packet_buffer[0]; // adamnet_recv();
    //adamnet_recv();
}


// Do SIO copy
void iwmFuji::iwm_copy_file()
{
}

/* 
// Mount all
bool iwmFuji::iwm_mount_all()
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
                return true;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                return true;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;
 
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

    // Go ahead and respond ok
    return false;
} 
*/

// Set boot mode
void iwmFuji::iwm_set_boot_mode()
{
    uint8_t bm = packet_buffer[0]; // adamnet_recv();
    
    insert_boot_device(bm);
    boot_config = true;
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
void iwmFuji::iwm_write_app_key()
{
    uint16_t creator = get_packet_length();//adamnet_recv_length();
    int idx = 0;
    uint8_t app = packet_buffer[idx++];//adamnet_recv();
    uint8_t key = packet_buffer[idx++];//adamnet_recv();
    uint8_t data[64];
    char appkeyfilename[30];
    FILE *fp;

    snprintf(appkeyfilename, sizeof(appkeyfilename), "/FujiNet/%04hx%02hhx%02hhx.key",creator,app,key);

    memcpy(data, &packet_buffer[idx], 64); // adamnet_recv_buffer(data,64);

    Debug_printf("Fuji Cmd: WRITE APPKEY %s\n",appkeyfilename);

    fp = fnSDFAT.file_open(appkeyfilename, "w");

    if (fp == nullptr)
    {
        Debug_printf("Could not open.\n");
        return;
    }
    
    size_t l = fwrite(data,sizeof(uint8_t),sizeof(data),fp);
    fclose(fp);
}

/*
 Read an "app key" from SD (ONLY!) storage
 // to do - Apple 2 should send a CTRL command to read app key then a STATUS command to get the app key 
 // can use the same CMD code to make it easier but maybe confusing - a little different protocol than adamnet
*/
void iwmFuji::iwm_ctrl_read_app_key()
{
    uint16_t creator = get_packet_length();//adamnet_recv_length();
    int idx = 0;
    uint8_t app = packet_buffer[idx++];//adamnet_recv();
    uint8_t key = packet_buffer[idx++];//adamnet_recv();
    
    char appkeyfilename[30];
    FILE *fp;

    snprintf(appkeyfilename, sizeof(appkeyfilename), "/FujiNet/%04hx%02hhx%02hhx.key",creator,app,key);

    fp = fnSDFAT.file_open(appkeyfilename, "r");

    if (fp == nullptr)
    {
        Debug_printf("Could not open key.");
        return;
    }

    memset(app_key_buffer,0,sizeof(app_key_buffer));
    app_key_len = fread(app_key_buffer, sizeof(char),64,fp);
    fclose(fp);
}

void iwmFuji::iwm_stat_read_app_key() // return the app key that was just read by the read app key control command
{
    memset(packet_buffer,0,sizeof(packet_buffer));
    memcpy(app_key_buffer,packet_buffer,app_key_len);
    packet_len = app_key_len;
}

// DEBUG TAPE
void iwmFuji::debug_tape()
{
}

// Disk Image Unmount
void iwmFuji::iwm_disk_image_umount()
{
    unsigned char ds = packet_buffer[0];//adamnet_recv();
    
    _fnDisks[ds].disk_dev.unmount();
    _fnDisks[ds].reset();
}


//==============================================================================================================================

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void iwmFuji::image_rotate()
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
            _adamnet_bus->changeDeviceId(&_fnDisks[n].disk_dev, swap); // to do!
        }

        // The first slot gets the device ID of the last slot
        _adamnet_bus->changeDeviceId(&_fnDisks[0].disk_dev, last_id);
    }
}

// This gets called when we're about to shutdown/reboot
void iwmFuji::shutdown()
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

    AdamNet.start_time = esp_timer_get_time();

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
    else
    {
        AdamNet.start_time = esp_timer_get_time();
        adamnet_response_ack();
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
        if (maxlen == 31)
        {
            memmove(&dirpath[2], dirpath, 254);
            if (strstr(dirpath, ".DDP") || strstr(dirpath, ".ddp"))
            {
                dirpath[0] = 0x85;
                dirpath[1] = 0x86;
            }
            else if (strstr(dirpath, ".DSK") || strstr(dirpath, ".dsk"))
            {
                dirpath[0] = 0x87;
                dirpath[1] = 0x88;
            }
            else if (strstr(dirpath, ".ROM") || strstr(dirpath, ".rom"))
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
        }

        memset(response, 0, sizeof(response));
        memcpy(response, dirpath, maxlen);
        response_len = maxlen;
    }
    else
    {
        AdamNet.start_time = esp_timer_get_time();
        adamnet_response_ack();
    }
}

void adamFuji::adamnet_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();

    adamnet_recv(); // ck

    response_len = sizeof(pos);
    memcpy(response, &pos, sizeof(pos));

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();
}

void adamFuji::adamnet_set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");

    // DAUX1 and DAUX2 hold the position to seek to in low/high order
    uint16_t pos = 0;

    adamnet_recv_buffer((uint8_t *)&pos, sizeof(uint16_t));

    Debug_printf("pos is now %u", pos);

    adamnet_recv(); // ck

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    _fnHosts[_current_open_directory_slot].dir_seek(pos);
}

void adamFuji::adamnet_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    adamnet_recv(); // ck

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

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

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

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
}

//  Make new disk and shove into device slot
void adamFuji::adamnet_new_disk()
{
    uint8_t hs = adamnet_recv();
    uint8_t ds = adamnet_recv();
    uint32_t numBlocks;
    uint8_t *c = (uint8_t *)&numBlocks;
    uint8_t p[256];

    adamnet_recv_buffer(c, sizeof(uint32_t));
    adamnet_recv_buffer(p, 256);

    adamnet_recv(); // CK

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (host.file_exists((const char *)p))
    {
        AdamNet.start_time = esp_timer_get_time();
        adamnet_response_ack();
        return;
    }

    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *)p, 256);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");

    Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

    disk.disk_dev.write_blank(disk.fileh, numBlocks);

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    fclose(disk.fileh);
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

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();
}

// Read and save host slot data from computer
void adamFuji::adamnet_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    adamnet_recv_buffer((uint8_t *)hostSlots, sizeof(hostSlots));

    adamnet_recv(); // ck

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

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

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

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

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

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
    s--;
    s--;

    Debug_printf("SET DEVICE SLOT %d filename\n", ds);

    adamnet_recv_buffer((uint8_t *)&f, s);

    Debug_printf("filename: %s\n", f);

    adamnet_recv(); // CK

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    memcpy(_fnDisks[ds].filename, f, MAX_FILENAME_LEN);
    _populate_config_from_slots();
}

// Get a 256 byte filename from device slot
void adamFuji::adamnet_get_device_filename()
{
    unsigned char ds = adamnet_recv();

    adamnet_recv();

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

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
        _fnDisks[0].disk_dev.mount(fBoot, config_atr, 262144, MEDIATYPE_DDP);        
        break;
    case 1:

        fBoot = fnSPIFFS.file_open(mount_all_atr);
        _fnDisks[0].disk_dev.mount(fBoot, mount_all_atr, 262144, MEDIATYPE_DDP);        
        break;
    }

    _fnDisks[0].disk_dev.is_config_device = true;
    _fnDisks[0].disk_dev.device_active = true;
}

void adamFuji::adamnet_enable_device()
{
    unsigned char d = adamnet_recv();

    adamnet_recv();

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    AdamNet.enableDevice(d);
}

void adamFuji::adamnet_disable_device()
{
    unsigned char d = adamnet_recv();

    adamnet_recv();

    AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    AdamNet.disableDevice(d);
}

// Initializes base settings and adds our devices to the SIO bus
void adamFuji::setup(systemBus *siobus)
{
    // set up Fuji device
    _adamnet_bus = siobus;

    _populate_slots_from_config();

    // Disable booting from CONFIG if our settings say to turn it off
    boot_config = false;

    // Disable status_wait if our settings say to turn it off
    status_wait_enabled = false;

    _adamnet_bus->addDevice(&_fnDisks[0].disk_dev, ADAMNET_DEVICEID_DISK);
    _adamnet_bus->addDevice(&_fnDisks[1].disk_dev, ADAMNET_DEVICEID_DISK + 1);
    _adamnet_bus->addDevice(&_fnDisks[2].disk_dev, ADAMNET_DEVICEID_DISK + 2);
    _adamnet_bus->addDevice(&_fnDisks[3].disk_dev, ADAMNET_DEVICEID_DISK + 3);

    Debug_printf("Config General Boot Mode: %u\n",Config.get_general_boot_mode());
    if (Config.get_general_boot_mode() == 0)
    {
        FILE *f = fnSPIFFS.file_open("/autorun.ddp");
        _fnDisks[0].disk_dev.mount(f, "/autorun.ddp", 262144, MEDIATYPE_DDP);
    }
    else
    {
        FILE *f = fnSPIFFS.file_open("/mount-and-boot.ddp");
        _fnDisks[0].disk_dev.mount(f, "/mount-and-boot.ddp", 262144, MEDIATYPE_DDP);
    }

    theNetwork = new adamNetwork();
    theSerial = new adamSerial();
    _adamnet_bus->addDevice(theNetwork, 0x09); // temporary.
    _adamnet_bus->addDevice(theSerial, 0x0e);  // Serial port
    _adamnet_bus->addDevice(&theFuji, 0x0F);   // Fuji becomes the gateway device.

    // Add our devices to the AdamNet bus
    // for (int i = 0; i < 4; i++)
    //    _adamnet_bus->addDevice(&_fnDisks[i].disk_dev, ADAMNET_DEVICEID_DISK + i);

    // for (int i = 0; i < MAX_NETWORK_DEVICES; i++)
    //     _adamnet_bus->addDevice(&sioNetDevs[i], ADAMNET_DEVICEID_FN_NETWORK + i);
}

// Mount all
void iwmFuji::sio_mount_all()
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
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
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
}

adamDisk *adamFuji::bootdisk()
{
    return _bootDisk;
}

//==============================================================================================================================


iwmDisk *iwmFuji::bootdisk()
{
    return nullptr;
}

void iwmFuji::insert_boot_device(uint8_t d)
{
}

void iwmFuji::setup(iwmBus *iwmbus)
{
}

void iwmFuji::image_rotate()
{
}
int iwmFuji::get_disk_id(int drive_slot)
{
    return -1;
}
std::string iwmFuji::get_host_prefix(int host_slot)
{
    return std::string();
}

void iwmFuji::_populate_slots_from_config()
{
}
void iwmFuji::_populate_config_from_slots()
{
}

void iwmFuji::sio_mount_all() // 0xD7 (yes, I know.)
{
}

void iwmFuji::encode_status_reply_packet()
{

  uint8_t checksum = 0;
  uint8_t data[4];

  // Build the contents of the packet
  data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  data[1] = 0;         // block size 1
  data[2] = 0;  // block size 2
  data[3] = 0; // block size 3

  packet_buffer[0] = 0xff; // sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;        // PBEGIN - start byte
  packet_buffer[7] = 0x80;        // DEST - dest id - host
  packet_buffer[8] = id(); //d.device_id; // SRC - source id - us
  packet_buffer[9] = 0x81;        // TYPE -status
  packet_buffer[10] = 0x80;       // AUX
  packet_buffer[11] = 0x80;       // STAT - data status
  packet_buffer[12] = 0x84;       // ODDCNT - 4 data bytes
  packet_buffer[13] = 0x80;       // GRP7CNT
  // 4 odd bytes
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08); // odd msb
  packet_buffer[15] = data[0] | 0x80;                                                                                               // data 1
  packet_buffer[16] = data[1] | 0x80;                                                                                               // data 2
  packet_buffer[17] = data[2] | 0x80;                                                                                               // data 3
  packet_buffer[18] = data[3] | 0x80;                                                                                               // data 4

  for (int i = 0; i < 4; i++)
  { // calc the data bytes checksum
    checksum ^= data[i];
  }
  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[19] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[20] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[21] = 0xc8; // PEND
  packet_buffer[22] = 0x00; // end of packet in buffer
}

void iwmFuji::encode_status_dib_reply_packet()
{
  int grpbyte, grpcount, i;
  int grpnum, oddnum;
  uint8_t checksum = 0, grpmsb;
  uint8_t group_buffer[7];
  uint8_t data[25];
  // data buffer=25: 3 x Grp7 + 4 odds
  grpnum = 3;
  oddnum = 4;

  //* write data buffer first (25 bytes) 3 grp7 + 4 odds
  // General Status byte
  // Bit 7: Block  device
  // Bit 6: Write allowed
  // Bit 5: Read allowed
  // Bit 4: Device online or disk in drive
  // Bit 3: Format allowed
  // Bit 2: Media write protected (block devices only)
  // Bit 1: Currently interrupting (//c only)
  // Bit 0: Currently open (char devices only)
  data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  data[1] = 0;         // block size 1
  data[2] = 0;  // block size 2
  data[3] = 0; // block size 3
  data[4] = 0x08;                    // ID string length - 11 chars
  data[5] = 'T';
  data[6] = 'H';
  data[7] = 'E';
  data[8] = '_';
  data[9] = 'F';
  data[10] = 'U';
  data[11] = 'J';
  data[12] = 'I';
  data[13] = ' ';
  data[14] = ' ';
  data[15] = ' ';
  data[16] = ' ';
  data[17] = ' ';
  data[18] = ' ';
  data[19] = ' ';
  data[20] = ' ';  // ID string (16 chars total)
  data[21] = SP_TYPE_BYTE_FUJINET; // Device type    - 0x02  harddisk
  data[22] = SP_SUBTYPE_BYTE_FUJINET; // Device Subtype - 0x0a
  data[23] = 0x00; // Firmware version 2 bytes
  data[24] = 0x01; //

  // print_packet ((uint8_t*) data,get_packet_length()); // debug
  // Debug_print(("\nData loaded"));
  // Calculate checksum of sector bytes before we destroy them
  for (int count = 0; count < 25; count++) // xor all the data bytes
    checksum = checksum ^ data[count];

  // Start assembling the packet at the rear and work
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  // grps of 7
  for (grpcount = grpnum - 1; grpcount >= 0; grpcount--) // 3
  {
    for (i = 0; i < 8; i++)
    {
      group_buffer[i] = data[i + oddnum + (grpcount * 7)];
    }
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[(14 + oddnum + 1) + (grpcount * 8)] = grpmsb | 0x80; // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[(14 + oddnum + 2) + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;
  }

  // odd byte
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08); // odd msb
  packet_buffer[15] = data[0] | 0x80;
  packet_buffer[16] = data[1] | 0x80;
  packet_buffer[17] = data[2] | 0x80;
  packet_buffer[18] = data[3] | 0x80;
  ;

  packet_buffer[0] = 0xff; // sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;
  packet_buffer[6] = 0xc3;        // PBEGIN - start byte
  packet_buffer[7] = 0x80;        // DEST - dest id - host
  packet_buffer[8] = id(); // d.device_id; // SRC - source id - us
  packet_buffer[9] = 0x81;        // TYPE -status
  packet_buffer[10] = 0x80;       // AUX
  packet_buffer[11] = 0x80;       // STAT - data status
  packet_buffer[12] = 0x84;       // ODDCNT - 4 data bytes
  packet_buffer[13] = 0x83;       // GRP7CNT - 3 grps of 7

  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[43] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[44] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[45] = 0xc8; // PEND
  packet_buffer[46] = 0x00; // end of packet in buffer
}

void iwmFuji::iwm_open(cmdPacket_t cmd)
{
  Debug_printf("\r\nOpen FujiNet Unit # %02x",cmd.g7byte1);
  encode_status_reply_packet();
  IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmFuji::iwm_close(cmdPacket_t cmd)
{
}


void iwmFuji::iwm_read(cmdPacket_t cmd)
{
  uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];

  uint16_t numbytes = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
  numbytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

  uint32_t addy = (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
  addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
  addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

  Debug_printf("\r\nDevice %02x Read %04x bytes from address %06x", source, numbytes, addy);


  // Debug_printf(" - ERROR - No image mounted");
  // encode_error_reply_packet(source, SP_ERR_OFFLINE);
  // IWM.iwm_send_packet((unsigned char *)packet_buffer);
  // return;

  memcpy(packet_buffer,"HELLO WORLD",11);
  encode_data_packet(source, 11);
  Debug_printf("\r\nsending data packet with %d elements ...", 11);
  //print_packet();
  IWM.iwm_send_packet((unsigned char *)packet_buffer);
}


void iwmFuji::iwm_status(cmdPacket_t cmd)
{
  uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];
  uint8_t status_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
  Debug_printf("\r\nDevice %02x Status Code %02x", source, status_code);
  Debug_printf("\r\nStatus List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);

  switch (status_code)
  {
  case IWM_FUJICMD_GET_SSID:
    iwm_net_get_ssid(); // 0xFE
    break;
  case IWM_FUJICMD_SCAN_NETWORKS:
    iwm_net_scan_networks(); // 0xFD
    break;
  case IWM_FUJICMD_GET_SCAN_RESULT:
    iwm_net_scan_result(); // 0xFC
    break;
  case IWM_FUJICMD_GET_WIFISTATUS: // 0xFA
    iwm_net_get_wifi_status();
    break
  default:
    // to do - send bad STATUS req error
    break;
  }
  encode_data_packet(packet_len);
  IWM.iwm_send_packet((unsigned char *)packet_buffer);
}


void iwmFuji::iwm_ctrl(cmdPacket_t cmd)
{
  uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];
  uint8_t control_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
  Debug_printf("\r\nDevice %02x Control Code %02x", source, control_code);
  Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
  IWM.iwm_read_packet_timeout(100, (uint8_t *)packet_buffer, BLOCK_PACKET_LEN);
  Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", packet_buffer[11] & 0x7f, packet_buffer[12] & 0x7f);
  decode_data_packet();
  print_packet((uint8_t *)packet_buffer);

  switch (control_code)
  {
  case 0xAA:
    iwm_dummy_command();
    break;
  case IWM_CTRL_RESET: // 0x00
  case IWM_FUJICMD_RESET: // 0xFF
    iwm_reset_fujinet();
    break;
  case IWM_FUJICMD_SET_SSID:
    iwm_net_set_ssid(); // 0xFB
    break;
  case IWM_FUJICMD_MOUNT_HOST: // 0xF9
    iwm_mount_host();
    break;
  case IWM_FUJICMD_MOUNT_IMAGE: // 0xF8
    iwm_disk_image_mount();
    break;
  case IWM_FUJICMD_CONFIG_BOOT: // 0xD9
    iwm_set_boot_config();
    break;
  case IWM_FUJICMD_COPY_FILE: // 0xD8
    iwm_copy_file();
    break;
  case IWM_FUJICMD_MOUNT_ALL: // 0xD7
    if (iwm_mount_all())
      {
        // return an error status
      }
    break;
  case IWM_FUJICMD_SET_BOOT_MODE: // 0xD6
    iwm_set_boot_mode();
    break;
  default: // to do - send bad CTRL error
    break;
  }
  encode_status_reply_packet();
  IWM.iwm_send_packet((unsigned char *)packet_buffer);
}


void iwmFuji::process(cmdPacket_t cmd)
{
  fnLedManager.set(LED_BUS, true);
  switch (cmd.command)
  {
  case 0x80: // status
    Debug_printf("\r\nhandling status command");
    iwm_status(cmd);
    break;
  case 0x81: // read block
    iwm_return_badcmd(cmd);
    break;
  case 0x82: // write block
    iwm_return_badcmd(cmd);
    break;
  case 0x83: // format
    iwm_return_badcmd(cmd);
    break;
  case 0x84: // control
    Debug_printf("\r\nhandling control command");
    iwm_ctrl(cmd);
    break;
  case 0x86: // open
    Debug_printf("\r\nhandling open command");
    iwm_open(cmd);
    break;
  case 0x87: // close
    Debug_printf("\r\nhandling close command");
    iwm_close(cmd);
    break;
  case 0x88: // read
    Debug_printf("\r\nhandling read command");
    iwm_read(cmd);
    break;
  case 0x89: // write
    iwm_return_badcmd(cmd);
    break;
  } // switch (cmd)
  fnLedManager.set(LED_BUS, false);
}


#endif /* BUILD_APPLE */