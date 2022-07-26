#ifdef BUILD_APPLE
#ifndef FUJI_H
#define FUJI_H
#include <cstdint>

#include "../../include/debug.h"
#include "bus.h"
#include "iwm/network.h"
#include "iwm/printer.h"

#include "fujiHost.h"
#include "fujiDisk.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 4 // to do for now
#define MAX_NETWORK_DEVICES 4

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define MAX_APPKEY_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

typedef struct
{
    char ssid[MAX_SSID_LEN + 1];
    char hostname[64];
    unsigned char localIP[4];
    unsigned char gateway[4];
    unsigned char netmask[4];
    unsigned char dnsIP[4];
    unsigned char macAddress[6];
    unsigned char bssid[6];
    char fn_version[15];
} AdapterConfig;

enum appkey_mode : uint8_t
{
    APPKEYMODE_READ = 0,
    APPKEYMODE_WRITE,
    APPKEYMODE_INVALID
};

struct appkey
{
    uint16_t creator = 0;
    uint8_t app = 0;
    uint8_t key = 0;
    appkey_mode mode = APPKEYMODE_INVALID;
    uint8_t reserved = 0;
} __attribute__((packed));

class iwmFuji : public iwmDevice
{
private:
    bool isReady = false;
    bool alreadyRunning = false; // Replace isReady and scanStarted with THIS.
    bool scanStarted = false;
    bool hostMounted[MAX_HOSTS];
    bool setSSIDStarted = false;
    uint8_t err_result = SP_ERR_NOERROR;

    //uint8_t response[1024]; // use packet_buffer instead
    //uint16_t response_len;

    // Response to SIO_FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        uint8_t rssi;
    } detail;

    iwmBus *_iwm_bus;

    fujiHost _fnHosts[MAX_HOSTS];

    fujiDisk _fnDisks[MAX_DISK_DEVICES];

    iwmNetwork *theNetwork;

    int _current_open_directory_slot = -1;

    iwmDisk *_bootDisk; // special disk drive just for configuration

    uint8_t bootMode = 0; // Boot mode 0 = CONFIG, 1 = MINI-BOOT

    uint8_t _countScannedSSIDs = 0;

    appkey _current_appkey;

    uint8_t ctrl_stat_buffer[767]; // what is proper length
    size_t ctrl_stat_len = 0; // max payload length is 767

    char dirpath[256];

protected:
    void iwm_dummy_command();                    // control 0xAA
    void iwm_hello_world();                      // status 0xAA
    void iwm_ctrl_reset_fujinet();               // control 0xFF
    void iwm_stat_net_get_ssid();                // status 0xFE
    void iwm_stat_net_scan_networks();           // status 0xFD
    void iwm_ctrl_net_scan_result();             // control 0xFC
    void iwm_stat_net_scan_result();             // status 0xFC
    void iwm_ctrl_net_set_ssid();                // control 0xFB
    void iwm_stat_net_get_wifi_status();         // status 0xFA
    void iwm_ctrl_mount_host();                  // 0xF9
    void iwm_ctrl_disk_image_mount();            // 0xF8
    void iwm_ctrl_open_directory();              // 0xF7
    void iwm_ctrl_read_directory_entry();        // 0xF6
    void iwm_stat_read_directory_entry();        // 0xF6

    void iwm_ctrl_close_directory();        // 0xF5
    void iwm_stat_read_host_slots();        // 0xF4
    void iwm_ctrl_write_host_slots();       // 0xF3
    void iwm_stat_read_device_slots();      // 0xF2
    void iwm_ctrl_write_device_slots();     // 0xF1
    void iwm_ctrl_disk_image_umount();      // 0xE9
    void iwm_stat_get_adapter_config();     // 0xE8
    void iwm_ctrl_new_disk();               // 0xE7
  /*  void adamnet_unmount_host();           // 0xE6
 */
   void iwm_stat_get_directory_position(); // 0xE5
   void iwm_ctrl_set_directory_position(); // 0xE4
/*
    void adamnet_set_hadamnet_index();         // 0xE3
 */
   void iwm_ctrl_set_device_filename();    // 0xE2

    void iwm_ctrl_set_host_prefix();        // 0xE1
    void iwm_stat_get_host_prefix();        // 0xE0
/*
    void adamnet_set_adamnet_external_clock(); // 0xDF
    */
    void iwm_ctrl_write_app_key();          // 0xDE
    void iwm_ctrl_read_app_key();           // 0xDD - control
    void iwm_stat_read_app_key();             // 0xDD - status
    /*
    void adamnet_open_app_key();           // 0xDC
    void adamnet_close_app_key();          // 0xDB
    */
    void iwm_ctrl_get_device_filename();    // 0xDA
    void iwm_stat_get_device_filename();    // 0xDA
    
    void iwm_ctrl_set_boot_config();            // 0xD9
    void iwm_ctrl_copy_file();                  // 0xD8
    void iwm_ctrl_set_boot_mode();              // 0xD6
    void iwm_ctrl_enable_device();          // 0xD5
    void iwm_ctrl_disable_device();         // 0xD4

    void shutdown() override;
    void process(cmdPacket_t cmd) override;

    void iwm_ctrl(cmdPacket_t cmd) override;
    void iwm_open(cmdPacket_t cmd) override;
    void iwm_close(cmdPacket_t cmd) override;
    void iwm_read(cmdPacket_t cmd) override;
    void iwm_status(cmdPacket_t cmd) override; 

    void encode_status_reply_packet() override;
    void encode_status_dib_reply_packet() override;

    void encode_extended_status_reply_packet() override{};
    void encode_extended_status_dib_reply_packet() override{};

public:
    bool boot_config = true;
    
    bool status_wait_enabled = true;
    
    iwmDisk *bootdisk();

    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup(iwmBus *iwmbus);

    void image_rotate();
    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    bool iwm_mount_all();              // 0xD7

    void FujiStatus(cmdPacket_t cmd) { iwm_status(cmd); }
    void FujiControl(cmdPacket_t cmd) { iwm_ctrl(cmd); }

    iwmFuji();

    // virtual void startup_hack() override { Debug_printf("\r\n Fuji startup hack"); }
};

extern iwmFuji theFuji;

#endif // FUJI_H
#endif /* BUILD_APPLE */