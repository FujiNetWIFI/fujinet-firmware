#include "fuji.h"
//#include "disk.h"

//File atrConfig;
// sioFuji theFuji;
//sioDisk configDisk;

FS *fileSystems[8];
TNFSFS TNFS[8]; // up to 8 TNFS servers
// could make a list of 8 pointers and create New TNFS objects at mounting and point to them
// might also need to make the FS pointers so that can use SD, SPIFFS, too

File dir[8];     // maybe only need on dir file pointer?
File atr[8];     // up to 8 disk drives
sioDisk sioD[8]; // use pointers and create objects as needed?
sioNetwork sioN[8];

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

    // Scan to computer
    WiFi.mode(WIFI_STA);
    totalSSIDs = WiFi.scanNetworks();
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
        strcpy(ssidInfo.ssid, WiFi.SSID(cmdFrame.aux1).c_str());
        ssidInfo.rssi = (char)WiFi.RSSI(cmdFrame.aux1);
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
        Debug_printf("Connecting to net: %s password: %s\n", netConfig.ssid, netConfig.password);
#endif
        WiFi.begin(netConfig.ssid, netConfig.password);
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
    char wifiStatus = WiFi.status();

    // Update WiFi Status LED
    if (wifiStatus == WL_CONNECTED)
        wifi_led(true);
    else
        wifi_led(false);

    sio_to_computer((byte *)&wifiStatus, 1, false);
}

/**
   SIO TNFS Server Mount
*/
void sioFuji::sio_tnfs_mount_host()
{
    bool err;
    unsigned char hostSlot = cmdFrame.aux1;

    // if already a TNFS host, then disconnect. Also, SD and SPIFFS are always running.
    if (TNFS[hostSlot].isConnected())
    {
        char host[256];
        TNFS[hostSlot].host(host);
        if (strcmp(hostSlots.host[hostSlot], host) == 0)
        {
            sio_complete();
            return;
        }
        else
        {
            TNFS[hostSlot].end();
        }
    }
    // check for SD or SPIFFS or something else in hostSlots.host[hostSlot]
    if (strcmp(hostSlots.host[hostSlot], "SD") == 0)
    {
        err = (SD.cardType() != CARD_NONE);
        fileSystems[hostSlot] = &SD;
    }
    else if (strcmp(hostSlots.host[hostSlot], "SPIFFS") == 0)
    {
        err = true;
        fileSystems[hostSlot] = &SPIFFS;
    }
    else
    {
        err = TNFS[hostSlot].begin(hostSlots.host[hostSlot], TNFS_PORT);
        fileSystems[hostSlot] = &TNFS[hostSlot];
    }
    // fileSystems[hostSlot] = make_shared point of TNFSFS
    if (!err)
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
#ifdef DEBUG
    Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n", deviceSlots.slot[deviceSlot].file, deviceSlots.slot[deviceSlot].hostSlot, flag, deviceSlot);
#endif

    atr[deviceSlot] = fileSystems[deviceSlots.slot[deviceSlot].hostSlot]->open(deviceSlots.slot[deviceSlot].file, flag);
    //todo: implement what does FETCH mean?
    //bool opened = tnfs_open(deviceSlot, options, false);
    if (!atr[deviceSlot])
    {
        sio_error();
    }
    else
    {
        sioD[deviceSlot].mount(&atr[deviceSlot]);
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
    sioD[deviceSlot].umount(); // close file and remove from sioDisk
    atr[deviceSlot] = File();  // clear file from slot
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

/**
   Open TNFS Directory
*/
void sioFuji::sio_tnfs_open_directory()
{
    char current_entry[256];
    byte hostSlot = cmdFrame.aux1;
    byte ck = sio_to_peripheral((byte *)&current_entry, sizeof(current_entry));

    if (sio_checksum((byte *)&current_entry, sizeof(current_entry)) != ck)
    {
        sio_error();
        return;
    }

#ifdef DEBUG
    Debug_print("FujiNet is opening directory: ");
    Debug_println(current_entry);
#endif

    //     if (current_entry[0] != '/')
    //     {
    //         current_entry[0] = '/';
    //         current_entry[1] = '\0';
    // #ifdef DEBUG
    //         Debug_print("No directory defined for reading, setting to: ");
    //         Debug_println(current_entry);
    // #endif
    //     }

    // Remove trailing slash
    if ((strlen(current_entry) > 1) && (current_entry[strlen(current_entry) - 1] == '/'))
        current_entry[strlen(current_entry) - 1] = 0x00;

    //dir[hostSlot] = fileSystems[hostSlot]->open("/", "r");
    dir[hostSlot] = fileSystems[hostSlot]->open(current_entry, "r");

    if (dir[hostSlot])
        sio_complete();
    else
        sio_error();
}

/**
   Read next TNFS Directory entry
*/
void sioFuji::sio_tnfs_read_directory_entry()
{
    char current_entry[256];
    byte hostSlot = cmdFrame.aux2;
    byte len = cmdFrame.aux1;
    //byte ret = tnfs_readdir(hostSlot);
    File f = dir[hostSlot].openNextFile();

    if (!f)
        current_entry[0] = 0x7F; // end of dir
    else
    {
        strcpy(current_entry, f.name());
        if (f.isDirectory())
        {
            int a = strlen(current_entry);
            if (current_entry[--a] != '/')
            {
                current_entry[a++] = '/';
                current_entry[a] = '\0';
            }
        }
    }
    sio_to_computer((byte *)&current_entry, len, false);
}

/**
   Close TNFS Directory
*/
void sioFuji::sio_tnfs_close_directory()
{
    byte hostSlot = cmdFrame.aux1;

    dir[hostSlot].close();
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
    strcpy(adapterConfig.ssid, netConfig.ssid);

#ifdef ESP8266
    strcpy(adapterConfig.hostname, WiFi.hostname().c_str());
#else
    strcpy(adapterConfig.hostname, WiFi.getHostname());
#endif

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

    WiFi.macAddress(adapterConfig.macAddress);
    strncpy((char *)adapterConfig.bssid, (const char *)WiFi.BSSID(), 6);

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
        strcpy(deviceSlots.slot[newDisk.deviceSlot].file, newDisk.filename);

        if (fileSystems[newDisk.hostSlot]->exists(newDisk.filename))
        {
#ifdef DEBUG
            Debug_printf("XXX ATR file already exists.\n");
#endif
            sio_error();
            return;
        }
        //if (tnfs_open(newDisk.deviceSlot, 0x03, true) == true) // create file
        File f = fileSystems[newDisk.hostSlot]->open(newDisk.filename, "w+");
        if (f) // create file
        {
            atr[newDisk.deviceSlot] = f;
// todo: mount ATR file to sioD[deviceSlt]
#ifdef DEBUG
            Debug_printf("Nice! Created file %s\n", deviceSlots.slot[newDisk.deviceSlot].file);
#endif
            // todo: decide where to put write_blank_atr() and implement it
            bool ok = sioD[newDisk.deviceSlot].write_blank_atr(&atr[newDisk.deviceSlot], newDisk.sectorSize, newDisk.numSectors);
            if (ok)
            {
#ifdef DEBUG
                Debug_printf("Nice! Wrote ATR data\n");
#endif
                // todo: make these calls for sioD ...
                //sioD[newDisk.deviceSlot].setSS(newDisk.sectorSize);
                //derive_percom_block(newDisk.deviceSlot, newDisk.sectorSize, newDisk.numSectors); // this is called in sioDisk::mount()
                sioD[newDisk.deviceSlot].mount(&atr[newDisk.deviceSlot]); // mount does all this
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
            Debug_printf("XXX Could not open file %s\n", deviceSlots.slot[newDisk.deviceSlot].file);
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
        sio_tnfs_open_directory();
        break;
    case 0xF6:
        sio_ack();
        sio_tnfs_read_directory_entry();
        break;
    case 0xF5:
        sio_ack();
        sio_tnfs_close_directory();
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
#ifdef ESP8266
    digitalWrite(PIN_LED, (onOff ? LOW : HIGH));
#elif defined(ESP32)
    digitalWrite(PIN_LED1, (onOff ? LOW : HIGH));
#endif
}

void sioFuji::begin()
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
        if (deviceSlots.slot[i].file[0] == 0x00)
        {
            deviceSlots.slot[i].hostSlot = 0xFF;
        }
    }
    configDisk.mount(&atrConfig); // set up a special disk drive not on the bus
}

sioDisk *sioFuji::disk()
{
    return &configDisk;
}