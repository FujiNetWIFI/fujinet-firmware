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

#include "../../qrcode/qrmanager.h"

#define MAX_SPDISK_DEVICES 8
#define MAX_DISK2_DEVICES 2 // for now until we add 3.5" disks
#define MAX_A2DISK_DEVICES (MAX_SPDISK_DEVICES + MAX_DISK2_DEVICES)

using IWMControlHandlers = std::function<void(const iwm_decoded_cmd_t &cmd)>;
using IWMStatusHandlers = std::function<void(const iwm_decoded_cmd_t &cmd)>;

class iwmFuji : public fujiDevice
{
private:

    void prodos_write_boot_sector(fnFile *f);
    void prodos_write_sos_sector(fnFile *f);
    void prodos_write_directory_sectors(fnFile *f);
    void prodos_write_bitmap(fnFile *f, uint32_t numBlocks);

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

    std::unordered_map<uint8_t, IWMControlHandlers> control_handlers;
    std::unordered_map<uint8_t, IWMStatusHandlers> status_handlers;

    QRManager _qrManager = QRManager();

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void iwm_dummy_command(const iwm_decoded_cmd_t &cmd);                     // control 0xAA
    void iwm_hello_world();                       // status 0xAA
    void iwm_stat_get_wifi_enabled();             // 0xEA
    void iwm_ctrl_new_disk(const iwm_decoded_cmd_t &cmd);                     // 0xE7
    void iwm_ctrl_enable_device(const iwm_decoded_cmd_t &cmd);                // 0xD5
    void iwm_ctrl_disable_device(const iwm_decoded_cmd_t &cmd);               // 0xD4
    void send_stat_get_enable();                  // 0xD1

    void iwm_stat_get_heap();                     // 0xC1

    void iwm_ctrl_qrcode_input(const iwm_decoded_cmd_t &cmd);                 // 0xBC
    void iwm_ctrl_qrcode_encode(const iwm_decoded_cmd_t &cmd);                // 0xBD
    void iwm_stat_qrcode_length();                // 0xBE
    void iwm_ctrl_qrcode_output(const iwm_decoded_cmd_t &cmd);                // 0xBF
    void iwm_stat_qrcode_output();                // 0xBF

    void iwm_ctrl(const iwm_decoded_cmd_t &cmd) override;
    void iwm_open(const iwm_decoded_cmd_t &cmd) override;
    void iwm_close(const iwm_decoded_cmd_t &cmd) override;
    void iwm_read(const iwm_decoded_cmd_t &cmd) override;
    void iwm_status(const iwm_decoded_cmd_t &cmd) override;

    iwm_device_info_block_t create_dib_reply_packet() override;
    iwm_device_status_block_t create_status_reply_packet() override;

public:
    iwmFuji();
    void setup() override;

    DISK_DEVICE *get_disk_dev(int i) override {
#ifndef DEV_RELAY_SLIP
      return i < MAX_SPDISK_DEVICES
        ? (DISK_DEVICE *) &_fnDisks[i].disk_dev
        : (DISK_DEVICE *) &_fnDisk2s[i - MAX_SPDISK_DEVICES];
#else
      return &_fnDisks[i].disk_dev;
#endif
    }

    // Being used by iwm/disk.cpp
    void handle_ctl_eject(uint8_t spid);
    void FujiStatus(const iwm_decoded_cmd_t &cmd) { iwm_status(cmd); }
    void FujiControl(const iwm_decoded_cmd_t &cmd) { iwm_ctrl(cmd); }

    // ============ Wrapped Fuji commands ============
    void fujicmd_close_directory() override;
    void fujicmd_read_directory_entry(size_t maxlen, uint8_t addtl) override;
    success_is_true fujicmd_set_device_filename_success(uint8_t deviceSlot, uint8_t host,
                                                        disk_access_flags_t mode) override;
};

extern iwmFuji platformFuji;

#endif // IWMFUJI_H
#endif /* BUILD_APPLE */
