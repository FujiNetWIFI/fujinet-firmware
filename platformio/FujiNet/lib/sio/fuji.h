#ifndef FUJI_H
#define FUJI_H
#include <Arduino.h>

#include "sio.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#endif

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
    //   cmdPtr[0xF4] = sio_read_hosts_slots;
    //   cmdPtr[0xF3] = sio_write_hosts_slots;
    //   cmdPtr[0xF2] = sio_read_device_slots;
    //   cmdPtr[0xF1] = sio_write_device_slots;
    //   cmdPtr[0xE9] = sio_disk_image_umount;
    //   cmdPtr[0xE8] = sio_get_adapter_config;
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

public:
};

#endif // guard