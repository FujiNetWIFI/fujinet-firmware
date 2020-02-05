#ifndef FUJI_H
#define FUJI_H
#include <Arduino.h>
#include "debug.h"

#include "sio.h"
#include "disk.h"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <FS.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#include <SPIFFS.h>
#endif

extern File atrConfig;
extern sioDisk configDisk;
extern sioFuji theFuji;

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

class sioFuji : public sioDevice
{
private:
    void sio_status() override;     // 'S'
    void sio_net_scan_networks();   // 0xFD
    void sio_net_scan_result();     // 0xFC
    void sio_net_set_ssid();        // 0xFB
    void sio_net_get_wifi_status(); // 0xFA
    //   cmdPtr[0xF9] = sio_tnfs_mount_host;
    //   cmdPtr[0xF8] = sio_disk_image_mount;
    //   cmdPtr[0xF7] = sio_tnfs_open_directory;
    //   cmdPtr[0xF6] = sio_tnfs_read_directory_entry;
    //   cmdPtr[0xF5] = sio_tnfs_close_directory;
    void sio_read_hosts_slots();   // 0xF4
    void sio_write_hosts_slots();  // 0xF3
    void sio_read_device_slots();  // 0xF2
    void sio_write_device_slots(); // 0xF1
    //   cmdPtr[0xE9] = sio_disk_image_umount;
    void sio_get_adapter_config(); // 0xE8
    //   cmdPtr[0xE7] = sio_new_disk;
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
        };
        unsigned char rawData[118];
    } adapterConfig;

    bool load_config = true;

public:
    bool config_state() { return load_config; }
    void begin();
};

#endif // guard