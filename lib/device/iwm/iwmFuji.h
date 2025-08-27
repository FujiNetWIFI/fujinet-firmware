#ifdef BUILD_APPLE
#ifndef IWMFUJI_H
#define IWMFUJI_H
#include "fujiDevice.h"

#include <functional>
#include <map>

#include "iwm/disk2.h"
#include "iwm/network.h"
#include "iwm/cpm.h"
#include "iwm/clock.h"

#define MAX_SPDISK_DEVICES 4
#define MAX_DISK2_DEVICES 2 // for now until we add 3.5" disks
#define MAX_A2DISK_DEVICES (MAX_SPDISK_DEVICES + MAX_DISK2_DEVICES)

using IWMCmdHandlers = std::function<void(iwm_decoded_cmd_t)>;
using IWMControlHandlers = std::function<void()>;
using IWMStatusHandlers = std::function<void()>;

class iwmFuji : public fujiDevice
{
private:
    // Response to SIO_FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN + 1];
        uint8_t rssi;
    } detail;

#ifndef DEV_RELAY_SLIP
    iwmDisk2 _fnDisk2s[MAX_DISK2_DEVICES];
#endif
    iwmNetwork *theNetwork;

    iwmCPM *theCPM;

    iwmClock *theClock;

    char _appkeyfilename[30]; // Temp storage for appkey filename, populated by open and read by read/write
    // map appkey open modes to key sizes. The open will set the appkey_size to correct value for subsequent reads to ensure the returned block is the correct size
    int appkey_size = 64;
    std::map<int, int> mode_to_keysize = {
        {0, 64},
        {2, 256}
    };

    std::unordered_map<uint8_t, IWMCmdHandlers> command_handlers;
    std::unordered_map<uint8_t, IWMControlHandlers> control_handlers;
    std::unordered_map<uint8_t, IWMStatusHandlers> status_handlers;

    bool hash_is_hex_output = false;

protected:
    void transaction_complete() override {}
    void transaction_error() override {}
    bool transaction_get(void *data, size_t len) override {
        if (len > sizeof(data_buffer))
            return false;
        memcpy((uint8_t *) data, data_buffer, len);
        return true;
    }
    void transaction_put(const void *data, size_t len, bool err) override {
        // Move into response.
        memcpy(data_buffer, data, len);
        data_len = len;
    }

    size_t setDirEntryDetails(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen);

    void iwm_dummy_command();                     // control 0xAA
    void iwm_hello_world();                       // status 0xAA
    void iwm_stat_net_scan_result();              // status 0xFC
    void iwm_stat_get_wifi_enabled();             // 0xEA
    void iwm_ctrl_new_disk();                     // 0xE7
    void iwm_ctrl_enable_device();                // 0xD5
    void iwm_ctrl_disable_device();               // 0xD4
    void send_stat_get_enable();                  // 0xD1

    void iwm_ctrl_hash_input();                   // 0xC8
    void iwm_ctrl_hash_compute(bool clear_data);  // 0xC7, 0xC3
    void iwm_stat_hash_length();                  // 0xC6
    void iwm_ctrl_hash_output();                  // 0xC5 set hash_is_hex_output
    void iwm_stat_hash_output();                  // 0xC5 write response
    void iwm_ctrl_hash_clear();                   // 0xC2
    void iwm_stat_get_heap();                     // 0xC1

    void iwm_ctrl_qrcode_input();                 // 0xBC
    void iwm_ctrl_qrcode_encode();                // 0xBD
    void iwm_stat_qrcode_length();                // 0xBE
    void iwm_ctrl_qrcode_output();                // 0xBF
    void iwm_stat_qrcode_output();                // 0xBF

    void process(iwm_decoded_cmd_t cmd) override;

    void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    void iwm_open(iwm_decoded_cmd_t cmd) override;
    void iwm_close(iwm_decoded_cmd_t cmd) override;
    void iwm_read(iwm_decoded_cmd_t cmd) override;
    void iwm_status(iwm_decoded_cmd_t cmd) override;

    void send_status_reply_packet() override;
    void send_status_dib_reply_packet() override;
    void send_extended_status_reply_packet() override {};
    void send_extended_status_dib_reply_packet() override {};

public:
    uint8_t err_result = SP_ERR_NOERROR;
    bool status_completed = false;
    uint8_t status_code;

    iwmFuji();
    void setup() override;

    DEVICE_TYPE *get_disk_dev(int i) override {
#ifndef DEV_RELAY_SLIP
      return i < MAX_SPDISK_DEVICES
        ? (DEVICE_TYPE *) &_fnDisks[i].disk_dev
        : (DEVICE_TYPE *) &_fnDisk2s[i - MAX_SPDISK_DEVICES];
#else
      return &_fnDisks[i].disk_dev;
#endif
    }

    // Being used by iwm/disk.cpp
    void handle_ctl_eject(uint8_t spid);
    void FujiStatus(iwm_decoded_cmd_t cmd) { iwm_status(cmd); }
    void FujiControl(iwm_decoded_cmd_t cmd) { iwm_ctrl(cmd); }

    // ============ Wrapped Fuji commands ============
    void fujicmd_reset() override;
    void fujicmd_close_directory() override;
    void fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl) override;
};

extern iwmFuji platformFuji;

#endif // IWMFUJI_H
#endif /* BUILD_APPLE */
