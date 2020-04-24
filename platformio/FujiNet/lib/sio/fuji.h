#ifndef FUJI_H
#define FUJI_H
#include <cstdint>
#include <Arduino.h>
#include "debug.h"

#include "sio.h"
#include "disk.h"
#include "network.h"
#include "tnfs.h"
#include "fujiFileSystem.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <FS.h>
#endif
#ifdef ESP32
//#include <WiFi.h>
#include <SPIFFS.h>
#endif
#include <SD.h>

#define MAX_FILESYSTEMS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 8

#define MAX_FILENAME_LEN 36
#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

//extern FS *fileSystems[8];
extern TNFSFS TNFS[8];
//extern File atr[8]; // up to 8 disk drives
//extern sioDisk sioD[8]; //
//extern sioNetwork sioN[8];


class sioFuji : public sioDevice
{
private:

    fujiFileSystem fnFileSystems[MAX_FILESYSTEMS];
    
    struct fndisks_t
    {
        File file;
        fujiFileSystem *fnfs = NULL;
    };
    fndisks_t fnDisks[MAX_DISK_DEVICES];

    bool validate_host_slot(uint8_t slot, const char *dgmsg = NULL);
    bool validate_device_slot(uint8_t slot, const char *dgmsg = NULL);

protected:
    File atrConfig;     // autorun.atr for FujiNet configuration
    sioDisk configDisk; // special disk drive just for configuration

    struct _hostslot
    {
        char hostname[MAX_HOSTNAME_LEN];
    };
    union {
        _hostslot slot[MAX_FILESYSTEMS];
        unsigned char rawData[sizeof(_hostslot) * MAX_FILESYSTEMS];
    } hostSlots;

    struct _devslot
    {
        unsigned char hostSlot;
        unsigned char mode;
        char filename[MAX_FILENAME_LEN];
    };
    union {
        _devslot slot[MAX_DISK_DEVICES];
        unsigned char rawData[sizeof(_devslot) * MAX_DISK_DEVICES];
    } deviceSlots;

    void sio_status() override;           // 'S'
    void sio_net_scan_networks();         // 0xFD
    void sio_net_scan_result();           // 0xFC
    void sio_net_set_ssid();              // 0xFB
    void sio_net_get_wifi_status();       // 0xFA
    void sio_tnfs_mount_host();           // 0xF9
    void sio_disk_image_mount();          // 0xF8
    void sio_open_directory();            // 0xF7
    void sio_read_directory_entry();      // 0xF6
    void sio_close_directory();           // 0xF5
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
        struct _ssidinf
        {
            char ssid[MAX_SSID_LEN];
            char rssi;
        } detail;
        unsigned char rawData[sizeof(_ssidinf)];
    } ssidInfo; // A single SSID entry

    union {
        struct _netconf
        {
            char ssid[MAX_SSID_LEN];
            char password[MAX_WIFI_PASS_LEN];
        } detail;
        unsigned char rawData[sizeof(_netconf)];
    } netConfig; //Network Configuration

    union {
        struct _adapterconfig
        {
            char ssid[32];
            char hostname[64];
            unsigned char localIP[4];
            unsigned char gateway[4];
            unsigned char netmask[4];
            unsigned char dnsIP[4];
            unsigned char macAddress[6];
            unsigned char bssid[6];
        } detail;
        unsigned char rawData[sizeof(_adapterconfig)];
    } adapterConfig;

public:
    bool load_config = true;
    sioDisk *disk();
    sioNetwork *network();
    void setup(sioBus &mySIO);
    int image_rotate();
    sioFuji();
};

#endif // FUJI_H
