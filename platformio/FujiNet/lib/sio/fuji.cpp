#include <cstdint>
#include "fuji.h"
#include "led.h"
#include "fnWiFi.h"
#include "fnSystem.h"

//#include "disk.h"

//File atrConfig;
//sioFuji theFuji;
//sioDisk configDisk;

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
    memset(&fnFileSystems, 0, sizeof(fnFileSystems));
    memset(&fnDisks, 0, sizeof(fnDisks));

    // Help debugging...
    for(int i =0; i < MAX_FILESYSTEMS; i++)
        fnFileSystems[i].slotid = i;
}

bool sioFuji::validate_host_slot(uint8_t slot, const char * dmsg)
{
    if(slot < MAX_HOSTS)
        return true;
#ifdef DEBUG
    if(dmsg == NULL)
        Debug_printf("!! Invalid host slot %hu\n", slot);
    else
        Debug_printf("!! Invalid host slot %hu @ %s\n", slot, dmsg);
#endif
    return false;    
}

bool sioFuji::validate_device_slot(uint8_t slot, const char * dmsg)
{
    if(slot < MAX_DISK_DEVICES)
        return true;
#ifdef DEBUG
    if(dmsg == NULL)
        Debug_printf("!! Invalid device slot %hu\n", slot);
    else
        Debug_printf("!! Invalid device slot %hu @ %s\n", slot, dmsg);
#endif
    return false;    
}

void sioFuji::sio_status()
{
    char ret[4] = {0, 0, 0, 0};

    sio_to_computer((byte *)ret, 4, false);
    return;
}

/**
   Scan for networks
*/
void sioFuji::sio_net_scan_networks()
{
    char ret[4] = {0, 0, 0, 0};
/*
    // Scan to computer
    WiFi.mode(WIFI_STA);

    int retries = NET_SCAN_NETWORKS_RETRIES;
    int result = 0;
    do
    {
        result = WiFi.scanNetworks();
#ifdef DEBUG
        Debug_printf("scanNetworks returned %d\n", result);
#endif
        // We're getting WIFI_SCAN_FAILED (-2) after attempting and failing to connect to a network
        // End any retry attempt if we got a non-negative value
        if (result >= 0)
            break;

    } while (--retries > 0);

    // Boundary check
    if (result < 0)
        result = 0;
    if (result > 50)
        result = 50;
*/
    totalSSIDs = fnWiFi.scan_networks();

    ret[0] = totalSSIDs;
    sio_to_computer((byte *)ret, 4, false);
}

/**
   Return scanned network entry
*/
void sioFuji::sio_net_scan_result()
{
    bool err = false;
    if (cmdFrame.aux1 < totalSSIDs)
    {
        fnWiFi.get_scan_result(cmdFrame.aux1, ssidInfo.detail.ssid, (uint8_t *)&ssidInfo.detail.rssi);
        //strcpy(ssidInfo.ssid, WiFi.SSID(cmdFrame.aux1).c_str());
        //ssidInfo.rssi = (char)WiFi.RSSI(cmdFrame.aux1);
    }
    else
    {
        memset(ssidInfo.rawData, 0x00, sizeof(ssidInfo.rawData));
        err = true;
    }

    sio_to_computer(ssidInfo.rawData, sizeof(ssidInfo.rawData), err);
}

/**
   Set SSID
*/
void sioFuji::sio_net_set_ssid()
{
    byte ck = sio_to_peripheral((byte *)&netConfig.rawData, sizeof(netConfig.rawData));

    if (sio_checksum(netConfig.rawData, sizeof(netConfig.rawData)) != ck)
    {
        sio_error();
    }
    else
    {
#ifdef DEBUG
        Debug_printf("Connecting to net: %s password: %s\n", netConfig.detail.ssid, netConfig.detail.password);
#endif
        fnWiFi.connect(netConfig.detail.ssid, netConfig.detail.password);
        // todo: add error checking?
        // UDP.begin(16384); // move to TNFS.begin
        sio_complete();
    }
}

/**
   SIO get WiFi Status
*/
void sioFuji::sio_net_get_wifi_status()
{
#ifdef DEBUG
    Debug_println("sio_net_get_wifi_status();");
#endif
    /* Pulled this out, as the LED is being updated elsewhere
    // Update WiFi Status LED
    if (wifiStatus == WL_CONNECTED)
        wifi_led(true);
    else
        wifi_led(false);
    */

    // WL_CONNECTED = 3, WL_DISCONNECTED = 6
    byte wifiStatus = fnWiFi.connected() ? 3 : 6;
    sio_to_computer((byte *)&wifiStatus, 1, false);
}

/**
   SIO TNFS Server Mount
*/
void sioFuji::sio_tnfs_mount_host()
{
     unsigned char hostSlot = cmdFrame.aux1;

    // Make sure we weren't given a bad hostSlot
    if(! validate_host_slot(hostSlot, "sio_tnfs_mount_hosts"))
    {
        sio_error();
        return;
    }

    if (!fnFileSystems[hostSlot].mount(hostSlots.slot[hostSlot].hostname))
        sio_error();
    else
        sio_complete();
}

/**
   SIO TNFS Disk Image Mount
*/
void sioFuji::sio_disk_image_mount()
{
    unsigned char deviceSlot = cmdFrame.aux1;
    unsigned char options = cmdFrame.aux2; // 1=R | 2=R/W | 128=FETCH
    char flag[3] = {'r', 0, 0};
    if (options == 2)
    {
        flag[1] = '+';
    }
    // Make sure we weren't given a bad hostSlot
    if(! validate_device_slot(deviceSlot))
    {
        sio_error();
        return;
    }

#ifdef DEBUG
    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n", 
        deviceSlots.slot[deviceSlot].filename, deviceSlots.slot[deviceSlot].hostSlot, flag, deviceSlot + 1);
#endif
    fnDisks[deviceSlot].file = 
        fnFileSystems[deviceSlots.slot[deviceSlot].hostSlot].fs()->open(deviceSlots.slot[deviceSlot].filename, flag);
    //todo: implement what does FETCH mean?
    //bool opened = tnfs_open(deviceSlot, options, false);
    if (!fnDisks[deviceSlot].file)
    {
        sio_error();
    }
    else
    {
        sioD[deviceSlot].mount(&fnDisks[deviceSlot].file);
        // moved all this stuff to .mount
        sio_complete();
    }
}

/**
   SIO TNFS Disk Image uMount
*/
void sioFuji::sio_disk_image_umount()
{
    unsigned char deviceSlot = cmdFrame.aux1;
    // Make sure we weren't given a bad deviceSlot
    if(! validate_device_slot(deviceSlot))
    {
        sio_error();
        return;
    }

    sioD[deviceSlot].umount(); // close file and remove from sioDisk
    fnDisks[deviceSlot].file = File();  // clear file from slot
    sio_complete();            // always completes.
}

/*
    SIO Disk Image Rotate
*/
int sioFuji::image_rotate()
{
    File *temp;

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
    char current_entry[256];
    byte hostSlot = cmdFrame.aux1;
    byte ck = sio_to_peripheral((byte *)&current_entry, sizeof(current_entry));

    if (sio_checksum((byte *)&current_entry, sizeof(current_entry)) != ck)
    {
        sio_error();
        return;
    }
    if(! validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }

#ifdef DEBUG
    Debug_printf("Opening directory: \"%s\"", current_entry);
#endif

    // Remove trailing slash
    if ((strlen(current_entry) > 1) && (current_entry[strlen(current_entry) - 1] == '/'))
        current_entry[strlen(current_entry) - 1] = 0x00;


    if( fnFileSystems[hostSlot].dir_open(current_entry) >= 0)
        sio_complete();
    else
        sio_error();
}

void sioFuji::sio_read_directory_entry()
{
#ifdef DEBUG
    Debug_println("sioFuji::sio_read_directory_entry");
#endif
    char current_entry[256];
    byte len = cmdFrame.aux1;
    byte hostSlot = cmdFrame.aux2;

    if(! validate_host_slot(hostSlot, "sio_read_directory_entry"))
    {
        sio_error();
        return;
    }

    //byte ret = tnfs_readdir(hostSlot);
    File f = fnFileSystems[hostSlot].dir_nextfile();
    int l = 0;

    if (!f)
    {
        current_entry[0] = 0x7F; // end of dir
#ifdef DEBUG
        Debug_println("Reached end of of directory");
#endif        
    }
    else
    {
        if (f.name()[0] == '/')
        {
            for (l = strlen(f.name()); l-- > 0;)
            {
                if (f.name()[l] == '/')
                {
                    l++;
                    break;
                }
            }
        }
        strcpy(current_entry, &f.name()[l]);
        if (f.isDirectory())
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
    byte *ce_ptr = (byte *)&current_entry[stidx];
    sio_to_computer(ce_ptr, len, false);
}

void sioFuji::sio_close_directory()
{
    byte hostSlot = cmdFrame.aux1;
#ifdef DEBUG
    Debug_println("Closing directory");
#endif
    if(!validate_host_slot(hostSlot))
    {
        sio_error();
        return;
    }
    fnFileSystems[hostSlot].dir_close();
    sio_complete();
}

/**
   Read hosts Slots
*/
void sioFuji::sio_read_hosts_slots()
{
    sio_to_computer(hostSlots.rawData, sizeof(hostSlots.rawData), false);
}

/**
   Read Device Slots
*/
void sioFuji::sio_read_device_slots()
{
    load_config = false;
    sio_to_computer(deviceSlots.rawData, sizeof(deviceSlots.rawData), false);
}

/**
   Write hosts slots
*/
void sioFuji::sio_write_hosts_slots()
{
    byte ck = sio_to_peripheral(hostSlots.rawData, sizeof(hostSlots.rawData));

    if (sio_checksum(hostSlots.rawData, sizeof(hostSlots.rawData)) == ck)
    {
        atrConfig.seek(91792, SeekSet);
        atrConfig.write(hostSlots.rawData, sizeof(hostSlots.rawData));
        atrConfig.flush();
        sio_complete();
    }
    else
        sio_error();
}

/**
   Write Device slots
*/
void sioFuji::sio_write_device_slots()
{
    byte ck = sio_to_peripheral(deviceSlots.rawData, sizeof(deviceSlots.rawData));

    if (sio_checksum(deviceSlots.rawData, sizeof(deviceSlots.rawData)) == ck)
    {
        atrConfig.seek(91408, SeekSet);
        atrConfig.write(deviceSlots.rawData, sizeof(deviceSlots.rawData));
        atrConfig.flush();
        sio_complete();
    }
    else
        sio_error();
}

/**
   Get Adapter config.
*/
void sioFuji::sio_get_adapter_config()
{
    memset((void *) adapterConfig.rawData, 0, sizeof(adapterConfig.rawData));

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
/*
        adapterConfig.localIP[0] = WiFi.localIP()[0];
        adapterConfig.localIP[1] = WiFi.localIP()[1];
        adapterConfig.localIP[2] = WiFi.localIP()[2];
        adapterConfig.localIP[3] = WiFi.localIP()[3];

        adapterConfig.gateway[0] = WiFi.gatewayIP()[0];
        adapterConfig.gateway[1] = WiFi.gatewayIP()[1];
        adapterConfig.gateway[2] = WiFi.gatewayIP()[2];
        adapterConfig.gateway[3] = WiFi.gatewayIP()[3];

        adapterConfig.netmask[0] = WiFi.subnetMask()[0];
        adapterConfig.netmask[1] = WiFi.subnetMask()[1];
        adapterConfig.netmask[2] = WiFi.subnetMask()[2];
        adapterConfig.netmask[3] = WiFi.subnetMask()[3];

        adapterConfig.dnsIP[0] = WiFi.dnsIP()[0];
        adapterConfig.dnsIP[1] = WiFi.dnsIP()[1];
        adapterConfig.dnsIP[2] = WiFi.dnsIP()[2];
        adapterConfig.dnsIP[3] = WiFi.dnsIP()[3];

        strncpy((char *)adapterConfig.bssid, (const char *)WiFi.BSSID(), 6);

*/
        strncpy(adapterConfig.detail.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(adapterConfig.detail.ssid));
        fnWiFi.get_current_bssid(adapterConfig.detail.bssid);
        fnSystem.Net.get_ip4_info(adapterConfig.detail.localIP, adapterConfig.detail.netmask, adapterConfig.detail.gateway);
        fnSystem.Net.get_ip4_dns_info(adapterConfig.detail.dnsIP);
    }
    
    //WiFi.macAddress(adapterConfig.macAddress);
    fnWiFi.get_mac(adapterConfig.detail.macAddress);

    sio_to_computer(adapterConfig.rawData, sizeof(adapterConfig.rawData), false);
}

/**
   Make new disk and shove into device slot
*/
void sioFuji::sio_new_disk()
{
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

    byte ck = sio_to_peripheral(newDisk.rawData, sizeof(newDisk));

    if (ck == sio_checksum(newDisk.rawData, sizeof(newDisk)))
    {
        deviceSlots.slot[newDisk.deviceSlot].hostSlot = newDisk.hostSlot;
        deviceSlots.slot[newDisk.deviceSlot].mode = 0x03; // R/W
        strcpy(deviceSlots.slot[newDisk.deviceSlot].filename, newDisk.filename);

        if (fnFileSystems[newDisk.hostSlot].fs()->exists(newDisk.filename))
        {
#ifdef DEBUG
            Debug_printf("XXX ATR file already exists.\n");
#endif
            sio_error();
            return;
        }
        //if (tnfs_open(newDisk.deviceSlot, 0x03, true) == true) // create file
        File f = fnFileSystems[newDisk.hostSlot].fs()->open(newDisk.filename, "w+");
        if (f) // create file
        {
            fnDisks[newDisk.deviceSlot].file = f;
// todo: mount ATR file to sioD[deviceSlt]
#ifdef DEBUG
            Debug_printf("Nice! Created file %s\n", deviceSlots.slot[newDisk.deviceSlot].filename);
#endif
            // todo: decide where to put write_blank_atr() and implement it
            bool ok = sioD[newDisk.deviceSlot].write_blank_atr(&fnDisks[newDisk.deviceSlot].file, newDisk.sectorSize, newDisk.numSectors);
            if (ok)
            {
#ifdef DEBUG
                Debug_printf("Nice! Wrote ATR data\n");
#endif
                // todo: make these calls for sioD ...
                //sioD[newDisk.deviceSlot].setSS(newDisk.sectorSize);
                //derive_percom_block(newDisk.deviceSlot, newDisk.sectorSize, newDisk.numSectors); // this is called in sioDisk::mount()
                sioD[newDisk.deviceSlot].mount(&fnDisks[newDisk.deviceSlot].file); // mount does all this
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

void sioFuji::sio_process()
{
    //   cmdPtr[0xE7] = sio_new_disk;
#ifdef DEBUG

#endif    
    switch (cmdFrame.comnd)
    {
    case 'S':
        sio_ack();
        sio_status();
        break;
    case 0xFD:
        sio_ack();
        sio_net_scan_networks();
        break;
    case 0xFC:
        sio_ack();
        sio_net_scan_result();
        break;
    case 0xFB:
        sio_ack();
        sio_net_set_ssid();
        break;
    case 0xFA:
        sio_ack();
        sio_net_get_wifi_status();
        break;
    case 0xF9:
        sio_ack();
        sio_tnfs_mount_host();
        break;
    case 0xF8:
        sio_ack();
        sio_disk_image_mount();
        break;
    case 0xF7:
        sio_ack();
        sio_open_directory();
        break;
    case 0xF6:
        sio_ack();
        sio_read_directory_entry();
        break;
    case 0xF5:
        sio_ack();
        sio_close_directory();
    case 0xF4:
        sio_ack();
        sio_read_hosts_slots(); // 0xF4
        break;
    case 0xF3:
        sio_ack();
        sio_write_hosts_slots(); // 0xF3
        break;
    case 0xF2:
        sio_ack();
        sio_read_device_slots(); // 0xF2
        break;
    case 0xF1:
        sio_ack();
        sio_write_device_slots(); // 0xF1
        break;
    case 0xE9:
        sio_ack();
        sio_disk_image_umount();
        break;
    case 0xE8:
        sio_ack();
        sio_get_adapter_config();
        break;
    case 0xE7:
        sio_ack();
        sio_new_disk();
        break;
    default:
        sio_nak();
    }
}

/**
   Set WiFi LED
*/
void sioFuji::wifi_led(bool onOff)
{
    ledMgr.set(eLed::LED_WIFI, onOff);
}

/*
    Initializes base settings and adds our devices to the SIO bus
*/
void sioFuji::setup(sioBus &mySIO)
{
    // set up Fuji device
    atrConfig = SPIFFS.open("/autorun.atr", "r+");

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
    configDisk.mount(&atrConfig); // set up a special disk drive not on the bus

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
