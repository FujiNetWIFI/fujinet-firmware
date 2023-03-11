#ifdef BUILD_IEC

#include "fuji.h"

#include <driver/ledc.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include "string_utils.h"

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnFsSPIFFS.h"
#include "fnWiFi.h"

#include "led.h"
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

// Status
void iecFuji::status()
{
    // TODO IMPLEMENT
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
    std::string r;
    char c[8];

    _countScannedSSIDs = fnWiFi.scan_networks();
    snprintf(c,sizeof(c),"%u\r",_countScannedSSIDs);
    response_queue.push(std::string(c));
}

// Return scanned network entry
void iecFuji::net_scan_result()
{
    std::vector<std::string> t = util_tokenize(payload, ':');

    // t[0] = SCANRESULT
    // t[1] = scan result # (0-numresults)
    // t[2] = RAW (optional)
    struct
    {
        char ssid[33];
        uint8_t rssi;
    } detail;

    if (t.size()>1)
    {
        int i = atoi(t[1].c_str());
        Debug_printf("Getting scan result %u\n",i);
        fnWiFi.get_scan_result(i,detail.ssid,&detail.rssi);
    }
    else
    {
        strcpy(detail.ssid,"INVALID SSID");
        detail.rssi = 0;
    }

    if (t.size() == 3) // SCANRESULT:0:RAW
    {
        std::string r = std::string((const char *)&detail,sizeof(detail));
        response_queue.push(r);
    }
    else // SCANRESULT:0
    {
        char c[40];
        std::string s = std::string(detail.ssid);
        mstr::toPETSCII(s);

        memset(c,0,sizeof(c));

        snprintf(c,40,"\"%s\",%d\r",s.c_str(),detail.rssi);
        response_queue.push(std::string(c,sizeof(c)));
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

    if (payload.find(":RAW") != std::string::npos)
    {
        std::string r = std::string((const char *)&cfg, sizeof(cfg));
        response_queue.push(r);
    }
    else // BASIC mode.
    {
        std::string r = std::string(cfg.ssid);
        mstr::toPETSCII(r);
        response_queue.push(r);
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

    if (payload.find("RAW:") != std::string::npos)
    {
        strncpy((char *)&cfg, payload.substr(12, std::string::npos).c_str(), sizeof(cfg));
    }
    else // easy BASIC form
    {
        mstr::toASCII(payload);
        std::string s = payload.substr(8, std::string::npos);
        std::vector<std::string> t = util_tokenize(s, ',');

        if (t.size() == 2)
        {
            strncpy(cfg.ssid, t[0].c_str(), 33);
            strncpy(cfg.password, t[1].c_str(), 64);
        }
    }

    Debug_printf("Storing WiFi SSID and Password.\n");
    Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
    Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
    Config.save();

    Debug_printf("Connecting to net %s\n", cfg.ssid);
    fnWiFi.connect(cfg.ssid, cfg.password);
}

// Get WiFi Status
void iecFuji::net_get_wifi_status()
{
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    char r[4];

    snprintf(r,sizeof(r),"%u\r",wifiStatus);

    response_queue.push(std::string(r));
}

// Check if Wifi is enabled
void iecFuji::net_get_wifi_enabled()
{
    // Not needed, will remove.
}

// Mount Server
void iecFuji::mount_host()
{
    std::vector<std::string> t = util_tokenize(payload,':');

    if (t.size()<2) // send error.
        return;
    
    int hs = atoi(t[1].c_str());

    if (!_validate_device_slot(hs,"mount_host"))
    {
        return; // send error.
    }

    if (!_fnHosts[hs].mount())
    {
        return; // send error.
    }

    // Otherwise, mount was successful.
}

// Disk Image Mount
void iecFuji::disk_image_mount()
{
    std::vector<std::string> t = util_tokenize(payload,':');
    if (t.size()<3)
    {
        // Error out, and return
        return;
    }

    uint8_t ds = atoi(t[1].c_str());
    uint8_t mode = atoi(t[2].c_str());

    char flag[3] = {'r',0,0};

    if (mode == DISK_ACCESS_MODE_WRITE)
        flag[1] = '+';

    if (!_validate_device_slot(ds))
    {
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
        // Send error
        return;
    }

    // We've gotten this far, so make sure our bootable CONFIG disk is disabled
    boot_config = false;

    // We need the file size for loading XEX files and for CASSETTE, so get that too
    disk.disk_size = host.file_size(disk.fileh);

    // And now mount it
    disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
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
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    std::vector<std::string> t = util_tokenize(payload,':');

    if (t.size()<3)
    {
        return; // send error
    }

    char dirpath[256];
    uint8_t hostSlot = atoi(t[1].c_str());
    strncpy(dirpath,t[2].c_str(),sizeof(dirpath));

    if (!_validate_host_slot(hostSlot))
    {
        // send error
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
    for (int i=0;i<sizeof(dirpath);i++)
    {
        if (dirpath[i]=='~')
            dirpath[i]=0; // turn into null
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
    }
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
    
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
    Debug_printf("get_adapter_config()\n");

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

    if (payload == "ADAPTERCONFIG:RAW")
    {
        std::string reply = std::string((const char *)&cfg, sizeof(AdapterConfig));
        response_queue.push(reply);
    }
    else if (payload == "ADAPTERCONFIG")
    {
        char reply[128];

        sprintf(reply, "%s\r", cfg.ssid);
        response_queue.push(std::string(reply));

        sprintf(reply, "%s\r", cfg.hostname);
        response_queue.push(std::string(reply));

        sprintf(reply, "%u.%u.%u.%u\r",
                cfg.localIP[0],
                cfg.localIP[1],
                cfg.localIP[2],
                cfg.localIP[3]);
        response_queue.push(std::string(reply));

        sprintf(reply, "%u.%u.%u.%u\r",
                cfg.netmask[0],
                cfg.netmask[1],
                cfg.netmask[2],
                cfg.netmask[3]);
        response_queue.push(std::string(reply));

        sprintf(reply, "%u.%u.%u.%u\r",
                cfg.gateway[0],
                cfg.gateway[1],
                cfg.gateway[2],
                cfg.gateway[3]);
        response_queue.push(std::string(reply));

        sprintf(reply, "%u.%u.%u.%u\r",
                cfg.dnsIP[0],
                cfg.dnsIP[1],
                cfg.dnsIP[2],
                cfg.dnsIP[3]);
        response_queue.push(std::string(reply));

        sprintf(reply, "%02X:%02X:%02X:%02X:%02X:%02X\r",
                cfg.macAddress[0],
                cfg.macAddress[1],
                cfg.macAddress[2],
                cfg.macAddress[3],
                cfg.macAddress[4],
                cfg.macAddress[5]);
        response_queue.push(std::string(reply));

        sprintf(reply, "%02X:%02X:%02X:%02X:%02X:%02X\r",
                cfg.bssid[0],
                cfg.bssid[1],
                cfg.bssid[2],
                cfg.bssid[3],
                cfg.bssid[4],
                cfg.bssid[5]);
        response_queue.push(std::string(reply));
    }
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
    
    _populate_slots_from_config();

    IEC.addDevice(this, 0x0F);
}

iecDisk *iecFuji::bootdisk()
{
    return &_bootDisk;
}

void iecFuji::tin()
{
    if (commanddata->secondary == IEC_REOPEN)
    {
        IEC.sendBytes("TESTING FROM OPEN.\r");
    }
}

void iecFuji::tout()
{
}

device_state_t iecFuji::process(IECData *id)
{
    virtualDevice::process(id);

    if (commanddata->channel != 15)
    {
        Debug_printf("Fuji device only accepts on channel 15. Sending NOTFOUND.\n");
        device_state = DEVICE_ERROR;
        IEC.senderTimeout();
    }
    else if (commanddata->primary != IEC_UNLISTEN)
        return device_state;

    if (payload.find("ADAPTERCONFIG") != std::string::npos)
        get_adapter_config();
    else if (payload.find("SETSSID") != std::string::npos)
        net_set_ssid();
    else if (payload.find("GETSSID") != std::string::npos)
        net_get_ssid();
    else if (payload.find("RESET") != std::string::npos)
        reset_fujinet();
    else if (payload.find("SCANRESULT") != std::string::npos)
        net_scan_result();
    else if (payload.find("SCAN") != std::string::npos)
        net_scan_networks();
    else if (payload.find("WIFISTATUS") != std::string::npos)
        net_get_wifi_status();
    else if (payload.find("MOUNTHOST") != std::string::npos)
        mount_host();
    else if (payload.find("MOUNTDRIVE") != std::string::npos)
        disk_image_mount();

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