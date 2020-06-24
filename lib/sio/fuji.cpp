#include <cstdint>
#include "fuji.h"
#include "led.h"
#include "fnWiFi.h"
#include "fnSystem.h"
#include "../FileSystem/fnFsSPIF.h"
#include "../config/fnConfig.h"
#include "../../include/version.h"

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
#define SIO_FUJICMD_STATUS 0x53

#define MAX_HOSTS 8

//FS *fileSystems[MAX_HOSTS];
//TNFSFS TNFS[MAX_HOSTS]; // up to 8 TNFS servers
// could make a list of 8 pointers and create New TNFS objects at mounting and point to them
// might also need to make the FS pointers so that can use SD, SPIFFS, too

//File dir[MAX_HOSTS];     // maybe only need on dir file pointer?
//File atr[MAX_HOSTS];     // up to 8 disk drives
sioDisk sioD[MAX_HOSTS]; // use pointers and create objects as needed?
sioNetwork sioN[MAX_HOSTS];

// Constructor
sioFuji::sioFuji()
{
    // Help debugging...
    for (int i = 0; i < MAX_FILESYSTEMS; i++)
        fnFileSystems[i].slotid = i;
}

bool sioFuji::validate_host_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_HOSTS)
        return true;
#ifdef DEBUG
    if (dmsg == NULL)
        Debug_printf("!! Invalid host slot %hu\n", slot);
    else
        Debug_printf("!! Invalid host slot %hu @ %s\n", slot, dmsg);
#endif
    return false;
}

bool sioFuji::validate_device_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_DISK_DEVICES)
        return true;
#ifdef DEBUG
    if (dmsg == NULL)
        Debug_printf("!! Invalid device slot %hu\n", slot);
    else
        Debug_printf("!! Invalid device slot %hu @ %s\n", slot, dmsg);
#endif
    return false;
}

void sioFuji::sio_status()
{
#ifdef DEBUG
    Debug_println("Fuji cmd: STATUS");
#endif
    char ret[4] = {0, 0, 0, 0};

    sio_to_computer((uint8_t *)ret, 4, false);
    return;
}

/*
   Reset FujiNet
 */
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
#ifdef DEBUG
    Debug_println("Fuji cmd: SCAN NETWORKS");
#endif
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
#ifdef DEBUG
    Debug_println("Fuji cmd: GET SCAN RESULT");
#endif
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
#ifdef DEBUG
    Debug_println("Fuji cmd: GET SSID");
#endif

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
#ifdef DEBUG
    Debug_println("Fuji cmd: SET SSID");
#endif
    uint8_t ck = sio_to_peripheral((uint8_t *)&netConfig.rawData, sizeof(netConfig.rawData));
    if (sio_checksum(netConfig.rawData, sizeof(netConfig.rawData)) != ck)
    {
        sio_error();
    }
    else
    {
        bool save = cmdFrame.aux1 != 0;
#ifdef DEBUG
        Debug_printf("Connecting to net: %s password: %s\n", netConfig.detail.ssid, netConfig.detail.password);
#endif
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
#ifdef DEBUG
    Debug_println("Fuji cmd: GET WIFI STATUS");
#endif

    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
    sio_to_computer((uint8_t *)&wifiStatus, 1, false);
}

/*
   SIO TNFS Server Mount
*/
void sioFuji::sio_tnfs_mount_host()
{
#ifdef DEBUG
    Debug_println("Fuji cmd: MOUNT HOST");
#endif
    unsigned char hostSlot = cmdFrame.aux1;

    // Make sure we weren't given a bad hostSlot
    if (!validate_host_slot(hostSlot, "sio_tnfs_mount_hosts"))
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
#ifdef DEBUG
    Debug_println("Fuji cmd: MOUNT IMAGE");
#endif
    unsigned char deviceSlot = cmdFrame.aux1;
    unsigned char options = cmdFrame.aux2; // 1=R | 2=R/W | 128=FETCH
    char flag[3] = {'r', 0, 0};
    if (options == 2)
    {
        flag[1] = '+';
    }
    // Make sure we weren't given a bad hostSlot
    if (!validate_device_slot(deviceSlot))
    {
        sio_error();
        return;
    }

#ifdef DEBUG
    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                 deviceSlots.slot[deviceSlot].filename, deviceSlots.slot[deviceSlot].hostSlot, flag, deviceSlot + 1);
#endif

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
#ifdef DEBUG
    Debug_println("Fuji cmd: UNMOUNT IMAGE");
#endif
    unsigned char deviceSlot = cmdFrame.aux1;
    // Make sure we weren't given a bad deviceSlot
    if (!validate_device_slot(deviceSlot))
    {
        sio_error();
        return;
    }

    sioD[deviceSlot].umount();          // close file and remove from sioDisk
    fnDisks[deviceSlot].file = nullptr; //= File();  // clear file from slot
    sio_complete();                     // always completes.
}

/*
    SIO Disk Image Rotate
*/
int sioFuji::image_rotate()
{
#ifdef DEBUG
    Debug_println("Fuji image rotate");
#endif

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
#ifdef DEBUG
    Debug_println("Fuji cmd: OPEN DIRECTORY");
#endif
    char current_entry[256];
    uint8_t hostSlot = cmdFrame.aux1;
    uint8_t ck = sio_to_peripheral((uint8_t *)&current_entry, sizeof(current_entry));

    if (sio_checksum((uint8_t *)&current_entry, sizeof(current_entry)) != ck)
    {
        sio_error();
        return;
    }
    if (!validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }

#ifdef DEBUG
    Debug_printf("Opening directory: \"%s\"\n", current_entry);
#endif

    // Remove trailing slash
    if ((strlen(current_entry) > 1) && (current_entry[strlen(current_entry) - 1] == '/'))
        current_entry[strlen(current_entry) - 1] = 0x00;

    if (fnFileSystems[hostSlot].dir_open(current_entry))
        sio_complete();
    else
        sio_error();
}

void sioFuji::sio_read_directory_entry()
{
#ifdef DEBUG
    Debug_println("Fuji cmd: READ DIRECTORY ENTRY");
#endif
    char current_entry[256];
    uint8_t len = cmdFrame.aux1;
    uint8_t hostSlot = cmdFrame.aux2;

    if (!validate_host_slot(hostSlot, "sio_read_directory_entry"))
    {
        sio_error();
        return;
    }

    //uint8_t ret = tnfs_readdir(hostSlot);
    fsdir_entry_t *f = fnFileSystems[hostSlot].dir_nextfile();
    int l = 0;

    if (f == nullptr)
    {
        current_entry[0] = 0x7F; // end of dir
        fnFileSystems[hostSlot].dir_close();
#ifdef DEBUG
        Debug_println("Reached end of of directory");
#endif
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

void sioFuji::sio_close_directory()
{
#ifdef DEBUG
    Debug_println("Fuji cmd: CLOSE DIRECTORY");
#endif
    uint8_t hostSlot = cmdFrame.aux1;

    if (!validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }
    fnFileSystems[hostSlot].dir_close();
    sio_complete();
}

/*
   Read hosts Slots
*/
void sioFuji::sio_read_hosts_slots()
{
#ifdef DEBUG
    Debug_println("Fuji cmd: READ HOST SLOTS");
#endif
    sio_to_computer(hostSlots.rawData, sizeof(hostSlots.rawData), false);
}

/*
   Read Device Slots
*/
void sioFuji::sio_read_device_slots()
{
#ifdef DEBUG
    Debug_println("Fuji cmd: READ DEVICE SLOTS");
#endif
    load_config = false;
    sio_to_computer(deviceSlots.rawData, sizeof(deviceSlots.rawData), false);
}

/*
   Write hosts slots
*/
void sioFuji::sio_write_hosts_slots()
{
#ifdef DEBUG
    Debug_println("Fuji cmd: WRITE HOST SLOTS");
#endif
    uint8_t ck = sio_to_peripheral(hostSlots.rawData, sizeof(hostSlots.rawData));

    if (sio_checksum(hostSlots.rawData, sizeof(hostSlots.rawData)) == ck)
    {
        /*
        atrConfig.seek(91792, SeekSet);
        atrConfig.write(hostSlots.rawData, sizeof(hostSlots.rawData));
        atrConfig.flush();
        */
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
#ifdef DEBUG
    Debug_println("Fuji cmd: WRITE DEVICE SLOTS");
#endif
    uint8_t ck = sio_to_peripheral(deviceSlots.rawData, sizeof(deviceSlots.rawData));

    if (sio_checksum(deviceSlots.rawData, sizeof(deviceSlots.rawData)) == ck)
    {
        /*
        atrConfig.seek(91408, SeekSet);
        atrConfig.write(deviceSlots.rawData, sizeof(deviceSlots.rawData));
        atrConfig.flush();
        */

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
#ifdef DEBUG
    Debug_println("Fuji cmd: GET ADAPTER CONFIG");
#endif
    memset((void *)adapterConfig.rawData, 0, sizeof(adapterConfig.rawData));

#ifdef ESP8266
    strncpy(adapterConfig.firmware, FN_VERSION_FULL, sizeof(adapterConfig.detail.firmware));
#else
    strncpy(adapterConfig.detail.firmware, FN_VERSION_FULL, sizeof(adapterConfig.detail.firmware));
#endif

    if (!fnWiFi.connected())
    {
        strncpy(adapterConfig.detail.ssid, "NOT CONNECTED", sizeof(adapterConfig.detail.ssid));
    }
    else
    {

#ifdef ESP8266
        strcpy(adapterConfig.hostname, WiFi.hostname().c_str());
#else
        strncpy(adapterConfig.detail.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(adapterConfig.detail.hostname));
#endif
        strncpy(adapterConfig.detail.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(adapterConfig.detail.ssid));
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
#ifdef DEBUG
    Debug_println("Fuji cmd: NEW DISK");
#endif
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
#ifdef DEBUG
            Debug_printf("XXX ATR file already exists.\n");
#endif
            sio_error();
            return;
        }
        // Create file
        FILE *f = fnFileSystems[newDisk.hostSlot].open(newDisk.filename, "w+");
        if (f != nullptr)
        {
            fnDisks[newDisk.deviceSlot].file = f;
#ifdef DEBUG
            Debug_printf("Nice! Created file %s\n", deviceSlots.slot[newDisk.deviceSlot].filename);
#endif

            bool ok = sioD[newDisk.deviceSlot].write_blank_atr(fnDisks[newDisk.deviceSlot].file, newDisk.sectorSize, newDisk.numSectors);
            if (ok)
            {
#ifdef DEBUG
                Debug_printf("Nice! Wrote ATR data\n");
#endif
                sioD[newDisk.deviceSlot].mount(fnDisks[newDisk.deviceSlot].file); // mount does all this
                sio_complete();
                return;
            }
            else
            {
#ifdef DEBUG
                Debug_printf("XXX ATR data write failed.\n");
#endif
                sio_error();
                return;
            }
        }
        else
        {
#ifdef DEBUG
            Debug_printf("XXX Could not open file %s\n", deviceSlots.slot[newDisk.deviceSlot].filename);
#endif
            sio_error();
            return;
        }
    }
    else
    {
#ifdef DEBUG
        Debug_printf("XXX Bad Checksum.\n");
#endif
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
            strncpy(hostSlots.slot[i].hostname,
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
                strncpy(deviceSlots.slot[i].filename,
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

    /*
    // Go ahead and read the host slots from disk
    atrConfig.seek(91792, SeekSet);
    atrConfig.read(hostSlots.rawData, 256);

    // And populate the device slots
    atrConfig.seek(91408, SeekSet);
    atrConfig.read(deviceSlots.rawData, 304);

    // Go ahead and mark all device slots local
    for (int i = 0; i < 8; i++)
    {
        if (deviceSlots.slot[i].filename[0] == 0x00)
        {
            deviceSlots.slot[i].hostSlot = 0xFF;
        }
    }
    */
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
#ifdef DEBUG
    Debug_println("sioFuji::sio_process() called");
#endif
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
    case SIO_FUJICMD_READ_HOST_SLOTS:
        sio_ack();
        sio_read_hosts_slots(); // 0xF4
        break;
    case SIO_FUJICMD_WRITE_HOST_SLOTS:
        sio_ack();
        sio_write_hosts_slots(); // 0xF3
        break;
    case SIO_FUJICMD_READ_DEVICE_SLOTS:
        sio_ack();
        sio_read_device_slots(); // 0xF2
        break;
    case SIO_FUJICMD_WRITE_DEVICE_SLOTS:
        sio_ack();
        sio_write_device_slots(); // 0xF1
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
