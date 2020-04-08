#ifndef FUJI_H
#define FUJI_H
#include <Arduino.h>
#include "debug.h"

#include "sio.h"
#include "disk.h"
#include "network.h"
#include "tnfs.h"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <FS.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#include <SPIFFS.h>
#endif
#include <SD.h>

#define NET_SCAN_NETWORKS_RETRIES 2

extern FS* fileSystems[8];
extern TNFSFS TNFS[8];
extern File atr[8];     // up to 8 disk drives
extern sioDisk sioD[8]; //
extern sioNetwork sioN[8];

class sioFuji : public sioDevice
{
protected:
    File atrConfig;     // autorun.atr for FujiNet configuration
    sioDisk configDisk; // special disk drive just for configuration

    union {
        char host[8][32];
        unsigned char rawData[256];
    } hostSlots;

    union {
        struct
        {
            unsigned char hostSlot;
            unsigned char mode;
            char file[36];
        } slot[8];
        unsigned char rawData[304];
    } deviceSlots;

    void sio_status() override;           // 'S'
    void sio_net_scan_networks();         // 0xFD
    void sio_net_scan_result();           // 0xFC
    void sio_net_set_ssid();              // 0xFB
    void sio_net_get_wifi_status();       // 0xFA
    void sio_tnfs_mount_host();           // 0xF9
    void sio_disk_image_mount();          // 0xF8
    void sio_tnfs_open_directory();       // 0xF7
    void sio_tnfs_read_directory_entry(); // 0xF6
    void sio_tnfs_close_directory();      // 0xF5
    void sio_read_hosts_slots();          // 0xF4
    void sio_write_hosts_slots();         // 0xF3
    void sio_read_device_slots();         // 0xF2
    void sio_write_device_slots();        // 0xF1
    void sio_disk_image_umount();         // 0xE9
    void sio_get_adapter_config();        // 0xE8
    void sio_new_disk();                  // 0xE7
    void sio_unmount_host();              // 0xE6 - new
    void wifi_led(bool onOff);

    void sio_process() override;

    char totalSSIDs;
    union {
        struct
        {
            char ssid[32];
            char rssi;
        };
        unsigned char rawData[33];
    } ssidInfo; // A single SSID entry

    union {
        struct
        {
            char ssid[32];
            char password[64];
        };
        unsigned char rawData[96];
    } netConfig; //Network Configuration

    union {
        struct
        {
            char ssid[32];
            char hostname[64];
            unsigned char localIP[4];
            unsigned char gateway[4];
            unsigned char netmask[4];
            unsigned char dnsIP[4];
            unsigned char macAddress[6];
            unsigned char bssid[6];
        };
        unsigned char rawData[124];
    } adapterConfig;

public:
    bool load_config = true;
    sioDisk *disk();
    sioNetwork *network();
    void begin();
    int image_rotate();
};

#endif // guard