#include <cstdint>

#include "fuji.h"
#include "led.h"
#include "fnWiFi.h"
#include "fnSystem.h"

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
#define SIO_FUJICMD_STATUS 0x53

#define MAX_HOSTS 8

sioDisk sioD[MAX_HOSTS]; // use pointers and create objects as needed?
sioNetwork sioN[MAX_HOSTS];

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
sioFuji::sioFuji()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_FILESYSTEMS; i++)
        fnFileSystems[i].slotid = i;
}

// Status
void sioFuji::sio_status()
{
    Debug_println("Fuji cmd: STATUS");

    char ret[4] = {0};

    sio_to_computer((uint8_t *)ret, sizeof(ret), false);
    return;
}

// Reset FujiNet
void sioFuji::sio_reset_fujinet()
{
    sio_complete();
    fnSystem.reboot();
}

/*
  Scan for networks
*/
void sioFuji::sio_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    char ret[4] = {0, 0, 0, 0};

    totalSSIDs = fnWiFi.scan_networks();

    ret[0] = totalSSIDs;
    sio_to_computer((uint8_t *)ret, 4, false);
}

/*
  Return scanned network entry
*/
void sioFuji::sio_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");

    bool err = false;
    if (cmdFrame.aux1 < totalSSIDs)
    {
        fnWiFi.get_scan_result(cmdFrame.aux1, ssidInfo.detail.ssid, (uint8_t *)&ssidInfo.detail.rssi);
    }
    else
    {
        memset(ssidInfo.rawData, 0x00, sizeof(ssidInfo.rawData));
        err = true;
    }

    sio_to_computer(ssidInfo.rawData, sizeof(ssidInfo.rawData), err);
}

/*
  Get SSID
*/
void sioFuji::sio_net_get_ssid()
{
    Debug_println("Fuji cmd: GET SSID");

    // TODO: Get rid of netConfig and use Config directly instead
    memset(netConfig.rawData, 0, sizeof(netConfig.rawData));
    memcpy(netConfig.detail.ssid, Config.get_wifi_ssid().c_str(),
           Config.get_wifi_ssid().length() > sizeof(netConfig.detail.ssid) ? sizeof(netConfig.detail.ssid) : Config.get_wifi_ssid().length());
    memcpy(netConfig.detail.password, Config.get_wifi_passphrase().c_str(),
           Config.get_wifi_passphrase().length() > sizeof(netConfig.detail.password) ? sizeof(netConfig.detail.password) : Config.get_wifi_passphrase().length());

    sio_to_computer(netConfig.rawData, sizeof(netConfig.rawData), false);
}

/*
   Set SSID
*/
void sioFuji::sio_net_set_ssid()
{
    Debug_println("Fuji cmd: SET SSID");

    uint8_t ck = sio_to_peripheral((uint8_t *)&netConfig.rawData, sizeof(netConfig.rawData));
    if (sio_checksum(netConfig.rawData, sizeof(netConfig.rawData)) != ck)
    {
        sio_error();
    }
    else
    {
        bool save = cmdFrame.aux1 != 0;

        Debug_printf("Connecting to net: %s password: %s\n", netConfig.detail.ssid, netConfig.detail.password);

        fnWiFi.connect(netConfig.detail.ssid, netConfig.detail.password);

        // Only save these if we're asked to, otherwise I assume it was a test for connectivity
        if (save)
        {
            Config.store_wifi_ssid(netConfig.detail.ssid, sizeof(netConfig.detail.ssid));
            Config.store_wifi_passphrase(netConfig.detail.password, sizeof(netConfig.detail.password));
            Config.save();
        }
        // todo: add error checking?
        sio_complete();
    }
}

/*
   SIO get WiFi Status
*/
void sioFuji::sio_net_get_wifi_status()
{
    Debug_println("Fuji cmd: GET WIFI STATUS");

    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    sio_to_computer((uint8_t *)&wifiStatus, 1, false);
}

/*
   SIO TNFS Server Mount
*/
void sioFuji::sio_tnfs_mount_host()
{
    Debug_println("Fuji cmd: MOUNT HOST");

    unsigned char hostSlot = cmdFrame.aux1;

    // Make sure we weren't given a bad hostSlot
    if (!_validate_host_slot(hostSlot, "sio_tnfs_mount_hosts"))
    {
        sio_error();
        return;
    }

    if (!fnFileSystems[hostSlot].mount(hostSlots.slot[hostSlot].hostname))
        sio_error();
    else
        sio_complete();
}

/*
   SIO TNFS Disk Image Mount
*/
void sioFuji::sio_disk_image_mount()
{
    Debug_println("Fuji cmd: MOUNT IMAGE");

    unsigned char deviceSlot = cmdFrame.aux1;
    unsigned char options = cmdFrame.aux2; // 1=R | 2=R/W | 128=FETCH
    char flag[3] = {'r', 0, 0};
    if (options == 2)
    {
        flag[1] = '+';
    }
    // Make sure we weren't given a bad hostSlot
    if (!_validate_device_slot(deviceSlot))
    {
        sio_error();
        return;
    }

    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 deviceSlots.slot[deviceSlot].filename, deviceSlots.slot[deviceSlot].hostSlot, flag, deviceSlot + 1);

    fnDisks[deviceSlot].file =
        fnFileSystems[deviceSlots.slot[deviceSlot].hostSlot].open(deviceSlots.slot[deviceSlot].filename, flag);

    //todo: implement what does FETCH mean?
    if (!fnDisks[deviceSlot].file)
    {
        sio_error();
    }
    else
    {
        sioD[deviceSlot].mount(fnDisks[deviceSlot].file);
        sio_complete();
    }
}

/*
   SIO TNFS Disk Image uMount
*/
void sioFuji::sio_disk_image_umount()
{
    Debug_println("Fuji cmd: UNMOUNT IMAGE");

    unsigned char deviceSlot = cmdFrame.aux1;
    // Make sure we weren't given a bad deviceSlot
    if (!_validate_device_slot(deviceSlot))
    {
        sio_error();
        return;
    }

    sioD[deviceSlot].umount();          // close file and remove from sioDisk
    fnDisks[deviceSlot].file = nullptr; //= File();  // clear file from slot
    sio_complete();                     // always completes.
}
void sioFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        sioD[i].umount();
}

/*
    SIO Disk Image Rotate
*/
int sioFuji::image_rotate()
{
    Debug_println("Fuji image rotate");

    FILE *temp;

    int n = 0;
    while (sioD[n].file() != nullptr)
    {
        n++;
    }

    if (n > 1)
    {
        n--;
        temp = sioD[n].file();
        for (int i = n; i > 0; i--)
        {
            sioD[i].mount(sioD[i - 1].file());
        }
        sioD[0].mount(temp);
    }
    return n;
}

void sioFuji::sio_open_directory()
{
    Debug_println("Fuji cmd: OPEN DIRECTORY");

    char current_entry[256];
    uint8_t hostSlot = cmdFrame.aux1;
    uint8_t ck = sio_to_peripheral((uint8_t *)&current_entry, sizeof(current_entry));

    if (sio_checksum((uint8_t *)&current_entry, sizeof(current_entry)) != ck)
    {
        sio_error();
        return;
    }
    if (!_validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }

    // If we already have a directory open, close it first
    if(_current_open_directory_slot != -1)
    {
        Debug_print("Directory was already open - closign it first\n");
        fnFileSystems[_current_open_directory_slot].dir_close();
        _current_open_directory_slot = -1;
    }

    Debug_printf("Opening directory: \"%s\"\n", current_entry);

    // Remove trailing slash
    if ((strlen(current_entry) > 1) && (current_entry[strlen(current_entry) - 1] == '/'))
        current_entry[strlen(current_entry) - 1] = 0x00;

    if (fnFileSystems[hostSlot].dir_open(current_entry))
    {
        _current_open_directory_slot = hostSlot;
        sio_complete();
    }
    else
        sio_error();
}

void sioFuji::sio_read_directory_entry()
{
    Debug_println("Fuji cmd: READ DIRECTORY ENTRY");

    char current_entry[256];
    uint8_t len = cmdFrame.aux1;
    //uint8_t hostSlot = cmdFrame.aux2;

    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        sio_error();
        return;
    }
    /*
    if (!_validate_host_slot(hostSlot, "sio_read_directory_entry"))
    {
        sio_error();
        return;
    }
    */

    //fsdir_entry_t *f = fnFileSystems[hostSlot].dir_nextfile();
    fsdir_entry_t *f = fnFileSystems[_current_open_directory_slot].dir_nextfile();
    int l = 0;

    if (f == nullptr)
    {
        current_entry[0] = 0x7F; // end of dir
        // fnFileSystems[hostSlot].dir_close(); // We should wait for an explicit close command
        Debug_println("Reached end of of directory");
    }
    else
    {
        if (f->filename[0] == '/')
        {
            for (l = strlen(f->filename); l-- > 0;)
            {
                if (f->filename[l] == '/')
                {
                    l++;
                    break;
                }
            }
        }
        strcpy(current_entry, &f->filename[l]);
        if (f->isDir)
        {
            int a = strlen(current_entry);
            if (current_entry[a - 1] != '/')
            {
                current_entry[a] = '/';
                current_entry[a + 1] = '\0';
                //Debug_println("append trailing /");
            }
        }
    }

    int stidx = 0;
    if (current_entry[0] == '/')
    {
        stidx = 1;
        //Debug_println("strip leading /");
    }

    uint8_t *ce_ptr = (uint8_t *)&current_entry[stidx];
    sio_to_computer(ce_ptr, len, false);
}

void sioFuji::sio_get_directory_position()
{
    Debug_println("Fuji cmd: GET DIRECTORY POSITION");

    /*
    uint8_t hostSlot = cmdFrame.aux1;

    if (!_validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }
    */
    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        sio_error();
        return;
    }

    uint16_t pos = fnFileSystems[_current_open_directory_slot].dir_tell();
    if (pos == FNFS_INVALID_DIRPOS)
    {
        sio_error();
        return;
    }
    // Return the value we read
    sio_to_computer((uint8_t *)&pos, sizeof(pos), false);
}

void sioFuji::sio_set_directory_position()
{
    Debug_println("Fuji cmd: SET DIRECTORY POSITION");
    /*
    uint8_t hostSlot = cmdFrame.aux1;

    if (!_validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }
    */

    // DAUX1 and DAUX2 hold the position to seek to in low/high order
    uint16_t pos = ((uint16_t)cmdFrame.aux2 << 8 | cmdFrame.aux1);
    // Make sure we have a current open directory
    if (_current_open_directory_slot == -1)
    {
        Debug_print("No currently open directory\n");
        sio_error();
        return;
    }

    bool result = fnFileSystems[_current_open_directory_slot].dir_seek(pos);
    if (result == false)
    {
        sio_error();
        return;
    }
    sio_complete();
}

void sioFuji::sio_close_directory()
{
    Debug_println("Fuji cmd: CLOSE DIRECTORY");

    //uint8_t hostSlot = cmdFrame.aux1;
    /*
    if (!_validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }
    */
    if(_current_open_directory_slot != -1)
        fnFileSystems[_current_open_directory_slot].dir_close();

    _current_open_directory_slot = -1;    
    sio_complete();
}

/*
   Read hosts Slots
*/
void sioFuji::sio_read_hosts_slots()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");

    sio_to_computer(hostSlots.rawData, sizeof(hostSlots.rawData), false);
}

/*
   Read Device Slots
*/
void sioFuji::sio_read_device_slots()
{
    Debug_println("Fuji cmd: READ DEVICE SLOTS");

    load_config = false;
    sio_to_computer(deviceSlots.rawData, sizeof(deviceSlots.rawData), false);
}

/*
   Write hosts slots
*/
void sioFuji::sio_write_hosts_slots()
{
    Debug_println("Fuji cmd: WRITE HOST SLOTS");

    uint8_t ck = sio_to_peripheral(hostSlots.rawData, sizeof(hostSlots.rawData));

    if (sio_checksum(hostSlots.rawData, sizeof(hostSlots.rawData)) == ck)
    {
        populate_config_from_slots();
        Config.save();

        sio_complete();
    }
    else
        sio_error();
}

/*
   Write Device slots
*/
void sioFuji::sio_write_device_slots()
{
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

    uint8_t ck = sio_to_peripheral(deviceSlots.rawData, sizeof(deviceSlots.rawData));

    if (sio_checksum(deviceSlots.rawData, sizeof(deviceSlots.rawData)) == ck)
    {
        populate_config_from_slots();
        Config.save();

        sio_complete();
    }
    else
        sio_error();
}

/*
   Get Adapter config.
*/
void sioFuji::sio_get_adapter_config()
{
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");

    memset((void *)adapterConfig.rawData, 0, sizeof(adapterConfig.rawData));

    strlcpy(adapterConfig.detail.fn_version, fnSystem.get_fujinet_version(true), sizeof(adapterConfig.detail.fn_version));

    if (!fnWiFi.connected())
    {
        strlcpy(adapterConfig.detail.ssid, "NOT CONNECTED", sizeof(adapterConfig.detail.ssid));
    }
    else
    {
        strlcpy(adapterConfig.detail.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(adapterConfig.detail.hostname));
        strlcpy(adapterConfig.detail.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(adapterConfig.detail.ssid));
        fnWiFi.get_current_bssid(adapterConfig.detail.bssid);
        fnSystem.Net.get_ip4_info(adapterConfig.detail.localIP, adapterConfig.detail.netmask, adapterConfig.detail.gateway);
        fnSystem.Net.get_ip4_dns_info(adapterConfig.detail.dnsIP);
    }

    //WiFi.macAddress(adapterConfig.macAddress);
    fnWiFi.get_mac(adapterConfig.detail.macAddress);

    sio_to_computer(adapterConfig.rawData, sizeof(adapterConfig.rawData), false);
}

/*
  Make new disk and shove into device slot
*/
void sioFuji::sio_new_disk()
{
    Debug_println("Fuji cmd: NEW DISK");

    union {
        struct
        {
            unsigned short numSectors;
            unsigned short sectorSize;
            unsigned char hostSlot;
            unsigned char deviceSlot;
            char filename[36];
        };
        unsigned char rawData[42];
    } newDisk;

    // Ask for details on the new disk to create
    uint8_t ck = sio_to_peripheral(newDisk.rawData, sizeof(newDisk));

    if (ck == sio_checksum(newDisk.rawData, sizeof(newDisk)))
    {
        deviceSlots.slot[newDisk.deviceSlot].hostSlot = newDisk.hostSlot;
        deviceSlots.slot[newDisk.deviceSlot].mode = 0x03; // R/W
        strcpy(deviceSlots.slot[newDisk.deviceSlot].filename, newDisk.filename);

        if (fnFileSystems[newDisk.hostSlot].exists(newDisk.filename))
        {
            Debug_printf("XXX ATR file already exists.\n");
            sio_error();
            return;
        }
        // Create file
        FILE *f = fnFileSystems[newDisk.hostSlot].open(newDisk.filename, "w+");
        if (f != nullptr)
        {
            fnDisks[newDisk.deviceSlot].file = f;
            Debug_printf("Nice! Created file %s\n", deviceSlots.slot[newDisk.deviceSlot].filename);

            bool ok = sioD[newDisk.deviceSlot].write_blank_atr(fnDisks[newDisk.deviceSlot].file, newDisk.sectorSize, newDisk.numSectors);
            fclose(fnDisks[newDisk.deviceSlot].file);
            
            if (ok)
            {
                Debug_printf("Nice! Wrote ATR data\n");
                sioD[newDisk.deviceSlot].mount(fnDisks[newDisk.deviceSlot].file); // mount does all this
                sio_complete();
                return;
            }
            else
            {
                Debug_printf("XXX ATR data write failed.\n");
                sio_error();
                return;
            }
        }
        else
        {
            Debug_printf("XXX Could not open file %s\n", deviceSlots.slot[newDisk.deviceSlot].filename);
            sio_error();
            return;
        }
    }
    else
    {
        Debug_printf("XXX Bad Checksum.\n");
        sio_error();
        return;
    }
}

// Temporary(?) function while we move from old config storage to new
void sioFuji::populate_slots_from_config()
{
    for (int i = 0; i < MAX_FILESYSTEMS; i++)
    {
        if (Config.get_host_type(i) == fnConfig::host_types::HOSTTYPE_INVALID)
            hostSlots.slot[i].hostname[0] = '\0';
        else
            strlcpy(hostSlots.slot[i].hostname,
                    Config.get_host_name(i).c_str(), MAX_HOSTNAME_LEN);
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        deviceSlots.slot[i].hostSlot = 0xFF;
        deviceSlots.slot[i].filename[0] = '\0';

        if (Config.get_mount_host_slot(i) != HOST_SLOT_INVALID)
        {
            if (Config.get_mount_host_slot(i) >= 0 && Config.get_mount_host_slot(i) <= MAX_FILESYSTEMS)
            {
                strlcpy(deviceSlots.slot[i].filename,
                        Config.get_mount_path(i).c_str(), MAX_FILENAME_LEN);
                deviceSlots.slot[i].hostSlot = Config.get_mount_host_slot(i);
                if (Config.get_mount_mode(i) == fnConfig::mount_modes::MOUNTMODE_WRITE)
                    deviceSlots.slot[i].mode = 2; // WRITE
                else
                    deviceSlots.slot[i].mode = 1; //READ
            }
        }
    }
}

// Temporary(?) function while we move from old config storage to new
void sioFuji::populate_config_from_slots()
{
    for (int i = 0; i < MAX_FILESYSTEMS; i++)
    {
        if (hostSlots.slot[i].hostname[0])
            Config.store_host(i, hostSlots.slot[i].hostname, fnConfig::host_types::HOSTTYPE_TNFS);
        else
            Config.clear_host(i);
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (deviceSlots.slot[i].hostSlot == 0xFF || deviceSlots.slot[i].filename[0] == '\0')
            Config.clear_mount(i);
        else
            Config.store_mount(i, deviceSlots.slot[i].hostSlot, deviceSlots.slot[i].filename,
                               (deviceSlots.slot[i].mode == 2) ? fnConfig::mount_modes::MOUNTMODE_WRITE : fnConfig::mount_modes::MOUNTMODE_READ);
    }
}

/*
  Initializes base settings and adds our devices to the SIO bus
*/
void sioFuji::setup(sioBus &mySIO)
{
    // set up Fuji device
    atrConfig = fnSPIFFS.file_open("/autorun.atr");

    populate_slots_from_config();

    configDisk.mount(atrConfig); // set up a special disk drive not on the bus
    configDisk.is_config_device = true;
    configDisk.device_active = false;

    // Add our devices to the SIO bus
    for (int i = 0; i < 8; i++)
    {
        mySIO.addDevice(&sioD[i], SIO_DEVICEID_DISK + i);
        mySIO.addDevice(&sioN[i], SIO_DEVICEID_FN_NETWORK + i);
    }
}

sioDisk *sioFuji::disk()
{
    return &configDisk;
}

void sioFuji::sio_process()
{
    Debug_println("sioFuji::sio_process() called");

    switch (cmdFrame.comnd)
    {
    case 0x3F:
        sio_ack();
        sio_high_speed();
        break;
    case SIO_FUJICMD_STATUS:
        sio_ack();
        sio_status();
        break;
    case SIO_FUJICMD_RESET:
        sio_ack();
        sio_reset_fujinet();
        break;
    case SIO_FUJICMD_SCAN_NETWORKS:
        sio_ack();
        sio_net_scan_networks();
        break;
    case SIO_FUJICMD_GET_SCAN_RESULT:
        sio_ack();
        sio_net_scan_result();
        break;
    case SIO_FUJICMD_SET_SSID:
        sio_ack();
        sio_net_set_ssid();
        break;
    case SIO_FUJICMD_GET_SSID:
        sio_ack();
        sio_net_get_ssid();
        break;
    case SIO_FUJICMD_GET_WIFISTATUS:
        sio_ack();
        sio_net_get_wifi_status();
        break;
    case SIO_FUJICMD_MOUNT_HOST:
        sio_ack();
        sio_tnfs_mount_host();
        break;
    case SIO_FUJICMD_MOUNT_IMAGE:
        sio_ack();
        sio_disk_image_mount();
        break;
    case SIO_FUJICMD_OPEN_DIRECTORY:
        sio_ack();
        sio_open_directory();
        break;
    case SIO_FUJICMD_READ_DIR_ENTRY:
        sio_ack();
        sio_read_directory_entry();
        break;
    case SIO_FUJICMD_CLOSE_DIRECTORY:
        sio_ack();
        sio_close_directory();
        break;
    case SIO_FUJICMD_GET_DIRECTORY_POSITION:
        sio_ack();
        sio_get_directory_position();
        break;
    case SIO_FUJICMD_SET_DIRECTORY_POSITION:
        sio_ack();
        sio_set_directory_position();
        break;
    case SIO_FUJICMD_READ_HOST_SLOTS:
        sio_ack();
        sio_read_hosts_slots();
        break;
    case SIO_FUJICMD_WRITE_HOST_SLOTS:
        sio_ack();
        sio_write_hosts_slots();
        break;
    case SIO_FUJICMD_READ_DEVICE_SLOTS:
        sio_ack();
        sio_read_device_slots();
        break;
    case SIO_FUJICMD_WRITE_DEVICE_SLOTS:
        sio_ack();
        sio_write_device_slots();
        break;
    case SIO_FUJICMD_UNMOUNT_IMAGE:
        sio_ack();
        sio_disk_image_umount();
        break;
    case SIO_FUJICMD_GET_ADAPTERCONFIG:
        sio_ack();
        sio_get_adapter_config();
        break;
    case SIO_FUJICMD_NEW_DISK:
        sio_ack();
        sio_new_disk();
        break;
    default:
        sio_nak();
    }
}
