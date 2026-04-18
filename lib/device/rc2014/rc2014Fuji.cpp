#ifdef BUILD_RC2014

#include "rc2014Fuji.h"

#include <cstring>

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"

#include "utils.h"
#include "string_utils.h"
#include "fuji_endian.h"

#include "../../encoding/base64.h"
#include "../../encoding/hash.h"

// TODO:
// - refactor rc2014_new_disk() before use

rc2014Fuji platformFuji;
fujiDevice *theFuji = &platformFuji;        // global fuji device object
rc2014Network *theNetwork; // global network device object (temporary)
rc2014Printer *thePrinter; // global printer

// Constructor
rc2014Fuji::rc2014Fuji() : fujiDevice(MAX_DISK_DEVICES, ".img", std::nullopt)
{
}

// Status
void rc2014Fuji::rc2014_control_status()
{
    uint8_t r[6] = {0x8F, 0x00, 0x04, 0x00, 0x00, 0x04};
    rc2014_send_buffer(r, 6);
}

// Reset FujiNet
void rc2014Fuji::rc2014_reset_fujinet()
{
    rc2014_send_ack();
    Debug_println("rc2014 RESET FUJINET");
    rc2014_send_complete();
    fnSystem.reboot();
}

// Scan for networks
void rc2014Fuji::rc2014_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    rc2014_send_ack();

    isReady = false;

    if (scanStarted == false)
    {
        _countScannedSSIDs = fnWiFi.scan_networks();
        scanStarted = true;
        setSSIDStarted = false;
    }

    isReady = true;

    response[0] = _countScannedSSIDs;
    response_len = 1;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

// Return scanned network entry
void rc2014Fuji::rc2014_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");
    scanStarted = false;

    uint8_t n = cmdFrame.aux1;

    rc2014_send_ack();

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
    response_len = 33;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

//  Get SSID
void rc2014Fuji::rc2014_net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    rc2014_send_ack();

    struct
    {
        char ssid[MAX_SSID_LEN+1];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    std::string s = Config.get_wifi_ssid();
    memcpy(cfg.ssid, s.c_str(),
           s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(cfg.password, s.c_str(),
           s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

    memcpy(response, &cfg, sizeof(cfg));
    response_len = sizeof(cfg);

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

// Set SSID
void rc2014Fuji::rc2014_net_set_ssid()
{
    if (!fnWiFi.connected() && setSSIDStarted == false)
    {
        Debug_println("Fuji cmd: SET SSID");

        rc2014_send_ack();

        struct
        {
            char ssid[MAX_SSID_LEN+1];
            char password[MAX_WIFI_PASS_LEN];
        } cfg;

        rc2014_recv_buffer((uint8_t *)&cfg, sizeof(cfg));
        rc2014_send_ack();

        bool save = true;

        Debug_printf("Connecting to net: %s password: %s (length: %d)\n", cfg.ssid, cfg.password, strlen(cfg.password));

        fnWiFi.connect(cfg.ssid, cfg.password);
        setSSIDStarted = true;
        if (save)
        {
            Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
            Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
            Config.save();
        }

    }
    rc2014_send_complete();
}

// Get WiFi Status
void rc2014Fuji::rc2014_net_get_wifi_status()
{
    rc2014_send_ack();
    Debug_println("Fuji cmd: GET WIFI STATUS");
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    response[0] = wifiStatus;
    response_len = 1;

    rc2014_send(response[0]);
    rc2014_flush();
    rc2014_send_complete();
}

// Mount Server
void rc2014Fuji::rc2014_mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");
    rc2014_send_ack();

    unsigned char hostSlot = cmdFrame.aux1;
    fujicore_mount_host_success(hostSlot);

    rc2014_send_complete();
}

// Disk Image Mount
void rc2014Fuji::rc2014_disk_image_mount()
{
    Debug_println("Fuji cmd: MOUNT IMAGE");
    rc2014_send_ack();

    uint8_t deviceSlot = cmdFrame.aux1;
    uint8_t options = cmdFrame.aux2; // DISK_ACCESS_MODE

    // TODO: Implement FETCH?
    char flag[3] = {'r', 0, 0};
    if (options == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    fujiDisk &disk = _fnDisks[deviceSlot];
    fujiHost &host = _fnHosts[disk.host_slot];

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 disk.filename, disk.host_slot, flag, deviceSlot + 1);

    rc2014_send_complete();

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

    boot_config = false;
    disk.disk_size = host.file_size(disk.fileh);
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size, disk.access_mode);
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void rc2014Fuji::rc2014_set_boot_config()
{
    Debug_println("Fuji cmd: SET BOOT CONFIG");
    rc2014_send_ack();
    boot_config = cmdFrame.aux1;
    rc2014_send_complete();
}

// Do SIO copy
void rc2014Fuji::rc2014_copy_file()
{
}

// Set boot mode
void rc2014Fuji::rc2014_set_boot_mode()
{
}

/*
 Opens an "app key".  This just sets the needed app key parameters (creator, app, key, mode)
 for the subsequent expected read/write command. We could've added this information as part
 of the payload in a WRITE_APPKEY command, but there was no way to do this for READ_APPKEY.
 Requiring a separate OPEN command makes both the read and write commands behave similarly
 and leaves the possibity for a more robust/general file read/write function later.
*/
void rc2014Fuji::rc2014_open_app_key()
{
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void rc2014Fuji::rc2014_close_app_key()
{
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void rc2014Fuji::rc2014_write_app_key()
{
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void rc2014Fuji::rc2014_read_app_key()
{
}

// DEBUG TAPE
void rc2014Fuji::debug_tape()
{
}

// Disk Image Unmount
void rc2014Fuji::rc2014_disk_image_umount()
{
    Debug_println("Fuji cmd: UMOUNT DISK IMAGE");
    rc2014_send_ack();

    unsigned char ds = cmdFrame.aux1;

    _fnDisks[ds].disk_dev.unmount();
    _fnDisks[ds].reset();

    rc2014_send_complete();
}

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void rc2014Fuji::image_rotate()
{
    Debug_println("Fuji cmd: IMAGE ROTATE");

    int count = 0;
    while (_fnDisks[count].fileh != nullptr)
        count++;

    if (count > 1)
    {
        count--;

        int last_id = _fnDisks[count].disk_dev.id();

        for (int n = count; n > 0; n--)
        {
            fujiDeviceID_t swap = (fujiDeviceID_t)_fnDisks[n - 1].disk_dev.id();
            Debug_printf("setting slot %d to ID %hx\n", n, swap);
            SYSTEM_BUS.changeDeviceId(&_fnDisks[n].disk_dev, swap);
        }

        SYSTEM_BUS.changeDeviceId(&_fnDisks[0].disk_dev, (fujiDeviceID_t)last_id);
    }
}

// This gets called when we're about to shutdown/reboot
void rc2014Fuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}

char dirpath[256];

void rc2014Fuji::rc2014_open_directory()
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    rc2014_send_ack();

    uint8_t hostSlot = cmdFrame.aux1;

    rc2014_recv_buffer((uint8_t *)&dirpath, 256);
    rc2014_send_ack();

    if (_current_open_directory_slot == -1)
    {
        const char *pattern = nullptr;
        int pathlen = strnlen(dirpath, sizeof(dirpath));
        if (pathlen < sizeof(dirpath) - 3)
        {
            pattern = dirpath + pathlen + 1;
            int patternlen = strnlen(pattern, sizeof(dirpath) - pathlen - 1);
            if (patternlen < 1)
                pattern = nullptr;
        }

        if (pathlen > 1 && dirpath[pathlen - 1] == '/')
            dirpath[pathlen - 1] = '\0';

        Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath, pattern ? pattern : "");

        if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
        {
            _current_open_directory_slot = hostSlot;
        }
    }

    rc2014_send_complete();
}

size_t rc2014Fuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    struct {
        dirEntryTimestamp modified;
        uint32_t size;
        uint8_t flags;
        uint8_t mediatype;
    } __attribute__((packed)) details;

    dirEntryDetails d = _additional_direntry_details(f);
    details.modified = d.modified;
    details.modified.year -= 100; // tm_year is since 1900; RC2014 protocol uses years since 2000
    details.size = htole32(d.size);
    details.flags = d.flags;
    details.mediatype = d.mediatype;

    maxlen -= sizeof(details);
    if (details.flags & DET_FF_DIR)
        maxlen--;

    if (strlen(f->filename) >= maxlen)
        details.flags |= DET_FF_TRUNC;

    memcpy(dest, &details, sizeof(details));
    return sizeof(details);
}

void rc2014Fuji::rc2014_read_directory_entry()
{
    uint8_t maxlen = cmdFrame.aux1;
    uint8_t addtl = cmdFrame.aux2;

    rc2014_send_ack();

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

        if (addtl & 0x80)
        {
            size_t details_len = set_additional_direntry_details(f, (uint8_t *)dirpath, maxlen);
            bufsize = sizeof(dirpath) - details_len;
            filenamedest = dirpath + details_len;
        }
        else
        {
            bufsize = maxlen;
        }

        int filelen;
        if (maxlen < 128)
        {
            filelen = util_ellipsize(f->filename, filenamedest, bufsize - 1);
        }
        else
        {
            filelen = strlcpy(filenamedest, f->filename, bufsize);
        }

        if (f->isDir && filelen < (bufsize - 2))
        {
            filenamedest[filelen] = '/';
            filenamedest[filelen + 1] = '\0';
        }
    }

    memset(response, 0, sizeof(response));
    memcpy(response, dirpath, maxlen);

    response_len = maxlen;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");
    rc2014_send_ack();

    uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
    response[0] = pos & 0xff;
    response[1] = (pos & 0xff00) >> 8;
    response_len = 2;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");
    rc2014_send_ack();

    uint16_t pos = (cmdFrame.aux2 << 8) | cmdFrame.aux1;

    Debug_printf("pos is now %u", pos);

    _fnHosts[_current_open_directory_slot].dir_seek(pos);

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    rc2014_send_ack();

    if (_current_open_directory_slot != -1)
        _fnHosts[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;
    response_len = 1;

    rc2014_send_complete();
}

// Get network adapter configuration
void rc2014Fuji::rc2014_get_adapter_config()
{
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    rc2014_send_ack();

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

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

//  Make new disk and shove into device slot
void rc2014Fuji::rc2014_new_disk()
{
    // TODO: FIX ME TO NEW PROTOCOL
    uint8_t hs = rc2014_recv();
    uint8_t ds = rc2014_recv();
    uint32_t numBlocks;
    uint8_t *c = (uint8_t *)&numBlocks;
    uint8_t p[256];

    rc2014_recv_buffer(c, sizeof(uint32_t));
    rc2014_recv_buffer(p, 256);

    rc2014_send_ack();

    fujiDisk &disk = _fnDisks[ds];
    fujiHost &host = _fnHosts[hs];

    if (host.file_exists((const char *)p))
    {
        rc2014_send_ack();
        return;
    }

    disk.host_slot = hs;
    disk.access_mode = DISK_ACCESS_MODE_WRITE;
    strlcpy(disk.filename, (const char *)p, 256);

    disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), "w");

    Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

    disk.disk_dev.write_blank(disk.fileh, numBlocks);

    rc2014_send_complete();

    fclose(disk.fileh);
}

// Send host slot data to computer
void rc2014Fuji::rc2014_read_host_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");
    rc2014_send_ack();

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    memset(hostSlots, 0, sizeof(hostSlots));

    for (int i = 0; i < MAX_HOSTS; i++)
        strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

    memcpy(response, hostSlots, sizeof(hostSlots));
    response_len = sizeof(hostSlots);

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

// Read and save host slot data from computer
void rc2014Fuji::rc2014_write_host_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    rc2014_send_ack();

    char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
    rc2014_recv_buffer((uint8_t *)hostSlots, sizeof(hostSlots));
    rc2014_send_ack();

    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].set_hostname(hostSlots[i]);

    populate_config_from_slots();
    Config.save();

    rc2014_send_complete();
}

// Store host path prefix
void rc2014Fuji::rc2014_set_host_prefix()
{
}

// Retrieve host path prefix
void rc2014Fuji::rc2014_get_host_prefix()
{
}

// Send device slot data to computer
void rc2014Fuji::rc2014_read_device_slots()
{
    Debug_println("Fuji cmd: READ DEVICE SLOTS");
    rc2014_send_ack();

    struct disk_slot
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    };
    disk_slot diskSlots[MAX_DISK_DEVICES];

    memset(&diskSlots, 0, sizeof(diskSlots));

    int returnsize;

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        diskSlots[i].mode = _fnDisks[i].access_mode;
        diskSlots[i].hostSlot = _fnDisks[i].host_slot;
        strlcpy(diskSlots[i].filename, _fnDisks[i].filename, MAX_DISPLAY_FILENAME_LEN);
    }

    returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;

    memcpy(response, &diskSlots, returnsize);
    response_len = returnsize;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

// Read and save disk slot data from computer
void rc2014Fuji::rc2014_write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");
    rc2014_send_ack();

    struct
    {
        uint8_t hostSlot;
        uint8_t mode;
        char filename[MAX_DISPLAY_FILENAME_LEN];
    } diskSlots[MAX_DISK_DEVICES];

    rc2014_recv_buffer((uint8_t *)&diskSlots, sizeof(diskSlots));
    rc2014_send_ack();

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, (disk_access_flags_t)diskSlots[i].mode);

    populate_config_from_slots();
    Config.save();

    rc2014_send_complete();
}

char f[MAX_FILENAME_LEN];

// Write a 256 byte filename to the device slot
void rc2014Fuji::rc2014_set_device_filename()
{
    unsigned char ds = cmdFrame.aux1;
    unsigned char flags = cmdFrame.aux2;

    Debug_printf("SET DEVICE SLOT %d filename\n", ds);
    rc2014_send_ack();

    rc2014_recv_buffer((uint8_t *)&f, 256);

    Debug_printf("filename: %s\n", f);

    rc2014_send_ack();

    memcpy(_fnDisks[ds].filename, f, MAX_FILENAME_LEN);
    _fnDisks[ds].access_mode = (disk_access_flags_t)flags;
    populate_config_from_slots();

    rc2014_send_complete();
}

// Get a 256 byte filename from device slot
void rc2014Fuji::rc2014_get_device_filename()
{
    unsigned char ds = cmdFrame.aux1;

    rc2014_send_ack();

    memcpy(response, _fnDisks[ds].filename, 256);
    response_len = 256;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_enable_device()
{
    unsigned char d = cmdFrame.aux1;

    rc2014_send_ack();

    SYSTEM_BUS.enableDevice((fujiDeviceID_t)d);

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_disable_device()
{
    unsigned char d = cmdFrame.aux1;

    rc2014_send_ack();

    SYSTEM_BUS.disableDevice((fujiDeviceID_t)d);

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_device_enabled_status()
{
    unsigned char d = cmdFrame.aux1;

    rc2014_send_ack();

    response[0] = (uint8_t)SYSTEM_BUS.enabledDeviceStatus((fujiDeviceID_t)d);
    response_len = 1;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_encode_input()
{
    Debug_printf("FUJI: BASE64 ENCODE INPUT\n");

    uint16_t len = (cmdFrame.aux2 << 8) | cmdFrame.aux1;
    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        rc2014_send_error();
        return;
    }

    std::vector<unsigned char> p(len);
    rc2014_send_ack();
    rc2014_recv_buffer((uint8_t *)p.data(), len);
    rc2014_send_ack();
    base64.base64_buffer += std::string((const char *)p.data(), len);
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_encode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 ENCODE COMPUTE\n");

    std::unique_ptr<char[]> p = Base64::encode(base64.base64_buffer.c_str(), base64.base64_buffer.size(), &out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        rc2014_send_error();
        return;
    }

    rc2014_send_ack();

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string(p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_encode_length()
{
    Debug_printf("FUJI: BASE64 ENCODE LENGTH\n");

    size_t l = base64.base64_buffer.length();
    if (!l)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
        rc2014_send_error();
    }

    Debug_printf("base64 buffer length: %u bytes\n",l);
    rc2014_send_ack();

    rc2014_send_buffer((uint8_t *)&l, sizeof(size_t));
    rc2014_flush();
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_encode_output()
{
    Debug_printf("FUJI: BASE64 ENCODE OUTPUT\n");

    uint16_t len = (cmdFrame.aux2 << 8) | cmdFrame.aux1;
    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        rc2014_send_error();
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        rc2014_send_error();
        return;
    }

    std::vector<unsigned char> p(len);
    rc2014_send_ack();

    memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();

    rc2014_send_buffer(p.data(), len);
    rc2014_flush();

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_decode_input()
{
    Debug_printf("FUJI: BASE64 DECODE INPUT\n");

    uint16_t len = (cmdFrame.aux2 << 8) | cmdFrame.aux1;
    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        rc2014_send_error();
        return;
    }

    std::vector<unsigned char> p(len);
    rc2014_send_ack();

    rc2014_recv_buffer((uint8_t *)p.data(), len);
    rc2014_send_ack();
    base64.base64_buffer += std::string((const char *)p.data(), len);
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_decode_compute()
{
    size_t out_len;

    Debug_printf("FUJI: BASE64 DECODE COMPUTE\n");

    std::unique_ptr<unsigned char[]> p = Base64::decode(base64.base64_buffer.c_str(),base64.base64_buffer.size(),&out_len);
    if (!p)
    {
        Debug_printf("base64_encode compute failed\n");
        rc2014_send_error();
        return;
    }

    rc2014_send_ack();

    base64.base64_buffer.clear();
    base64.base64_buffer = std::string((const char *)p.get(), out_len);

    Debug_printf("Resulting BASE64 encoded data is: %u bytes\n", out_len);
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_decode_length()
{
    Debug_printf("FUJI: BASE64 DECODE LENGTH\n");
    rc2014_send_ack();

    size_t len = base64.base64_buffer.length();

    if (!len)
    {
        Debug_printf("BASE64 buffer is 0 bytes, sending error.\n");
        rc2014_send_error();
        return;
    }

    Debug_printf("base64 buffer length: %u bytes\n", len);

    rc2014_send_buffer((uint8_t *)&len, sizeof(size_t));
    rc2014_flush();
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_base64_decode_output()
{
    Debug_printf("FUJI: BASE64 DECODE OUTPUT\n");

    uint16_t len = (cmdFrame.aux2 << 8) | cmdFrame.aux1;
    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        rc2014_send_error();
        return;
    }
    else if (len > base64.base64_buffer.length())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, base64.base64_buffer.length());
        rc2014_send_error();
        return;
    }

    std::vector<unsigned char> p(len);
    rc2014_send_ack();
    memcpy(p.data(), base64.base64_buffer.data(), len);
    base64.base64_buffer.erase(0, len);
    base64.base64_buffer.shrink_to_fit();

    rc2014_send_buffer(p.data(), len);
    rc2014_flush();

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_hash_input()
{
    Debug_printf("FUJI: HASH INPUT\n");

    uint16_t len = (cmdFrame.aux2 << 8) | cmdFrame.aux1;
    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        rc2014_send_error();
        return;
    }

    std::vector<unsigned char> p(len);
    rc2014_send_ack();
    rc2014_recv_buffer((uint8_t *)p.data(), len);
    rc2014_send_ack();
    hasher.add_data(p);

    rc2014_send_complete();
}

void rc2014Fuji::rc2014_hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(cmdFrame.aux1);
    rc2014_send_ack();
    hasher.compute(algorithm, clear_data);
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_hash_length()
{
    Debug_printf("FUJI: HASH LENGTH\n");
    bool is_hex = cmdFrame.aux1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);
    rc2014_send_ack();
    rc2014_send_buffer(&r, 1);
    rc2014_flush();
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT\n");
    uint16_t is_hex = cmdFrame.aux1;

    std::vector<uint8_t> hashed_data;
    if (is_hex) {
        std::string hex = hasher.output_hex();
        hashed_data = std::vector<uint8_t>(hex.begin(), hex.end());
    } else {
        hashed_data = hasher.output_binary();
    }

    rc2014_send_ack();
    rc2014_send_buffer(hashed_data.data(), hashed_data.size());
    rc2014_flush();
    rc2014_send_complete();
}

void rc2014Fuji::rc2014_hash_clear()
{
    Debug_printf("FUJI: HASH INIT\n");
    rc2014_send_ack();
    hasher.clear();
    rc2014_send_complete();
}

// Initializes base settings and adds our devices to the RC2014 bus
void rc2014Fuji::setup()
{
    populate_slots_from_config();

    boot_config = false;
    status_wait_enabled = false;

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        SYSTEM_BUS.addDevice(&_fnDisks[i].disk_dev,
                             static_cast<fujiDeviceID_t>(FUJI_DEVICEID_DISK + i));

    theNetwork = new rc2014Network();
    SYSTEM_BUS.addDevice(theNetwork, FUJI_DEVICEID_NETWORK);
    SYSTEM_BUS.addDevice(theFuji, FUJI_DEVICEID_FUJINET);
}

void rc2014Fuji::rc2014_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case FUJICMD_RESET:
        rc2014_reset_fujinet();
        break;
    case FUJICMD_SCAN_NETWORKS:
        rc2014_net_scan_networks();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        rc2014_net_scan_result();
        break;
    case FUJICMD_SET_SSID:
        rc2014_net_set_ssid();
        break;
    case FUJICMD_GET_SSID:
        rc2014_net_get_ssid();
        break;
    case FUJICMD_GET_WIFISTATUS:
        rc2014_net_get_wifi_status();
        break;
    case FUJICMD_MOUNT_HOST:
        rc2014_mount_host();
        break;
    case FUJICMD_MOUNT_IMAGE:
        rc2014_disk_image_mount();
        break;
    case FUJICMD_OPEN_DIRECTORY:
        rc2014_open_directory();
        break;
    case FUJICMD_READ_DIR_ENTRY:
        rc2014_read_directory_entry();
        break;
    case FUJICMD_CLOSE_DIRECTORY:
        rc2014_close_directory();
        break;
    case FUJICMD_GET_DIRECTORY_POSITION:
        rc2014_get_directory_position();
        break;
    case FUJICMD_SET_DIRECTORY_POSITION:
        rc2014_set_directory_position();
        break;
    case FUJICMD_READ_HOST_SLOTS:
        rc2014_read_host_slots();
        break;
    case FUJICMD_WRITE_HOST_SLOTS:
        rc2014_write_host_slots();
        break;
    case FUJICMD_READ_DEVICE_SLOTS:
        rc2014_read_device_slots();
        break;
    case FUJICMD_WRITE_DEVICE_SLOTS:
        rc2014_write_device_slots();
        break;
    case FUJICMD_UNMOUNT_IMAGE:
        rc2014_disk_image_umount();
        break;
    case FUJICMD_GET_ADAPTERCONFIG:
        rc2014_get_adapter_config();
        break;
    case FUJICMD_SET_DEVICE_FULLPATH:
        rc2014_set_device_filename();
        break;
    case FUJICMD_GET_DEVICE_FULLPATH:
        rc2014_get_device_filename();
        break;
    case FUJICMD_CONFIG_BOOT:
        rc2014_set_boot_config();
        break;
    case FUJICMD_MOUNT_ALL:
        fujicmd_mount_all_success();
        break;
    case FUJICMD_ENABLE_DEVICE:
        rc2014_enable_device();
        break;
    case FUJICMD_DISABLE_DEVICE:
        rc2014_disable_device();
        break;
    case FUJICMD_DEVICE_ENABLE_STATUS:
        rc2014_device_enabled_status();
        break;
    case FUJICMD_BASE64_ENCODE_INPUT:
        rc2014_base64_encode_input();
        break;
    case FUJICMD_BASE64_ENCODE_COMPUTE:
        rc2014_base64_encode_compute();
        break;
    case FUJICMD_BASE64_ENCODE_LENGTH:
        rc2014_base64_encode_length();
        break;
    case FUJICMD_BASE64_ENCODE_OUTPUT:
        rc2014_base64_encode_output();
        break;
    case FUJICMD_BASE64_DECODE_INPUT:
        rc2014_base64_decode_input();
        break;
    case FUJICMD_BASE64_DECODE_COMPUTE:
        rc2014_base64_decode_compute();
        break;
    case FUJICMD_BASE64_DECODE_LENGTH:
        rc2014_base64_decode_length();
        break;
    case FUJICMD_BASE64_DECODE_OUTPUT:
        rc2014_base64_decode_output();
        break;
    case FUJICMD_HASH_INPUT:
        rc2014_hash_input();
        break;
    case FUJICMD_HASH_COMPUTE:
        rc2014_hash_compute(true);
        break;
    case FUJICMD_HASH_COMPUTE_NO_CLEAR:
        rc2014_hash_compute(false);
        break;
    case FUJICMD_HASH_LENGTH:
        rc2014_hash_length();
        break;
    case FUJICMD_HASH_OUTPUT:
        rc2014_hash_output();
        break;
    case FUJICMD_HASH_CLEAR:
        rc2014_hash_clear();
        break;
    default:
        Debug_printf("rc2014_process() not implemented yet for this device. Cmd received: %02x\n", cmdFrame.comnd);
        rc2014_send_nak();
    }
}

#endif /* BUILD_RC2014 */
