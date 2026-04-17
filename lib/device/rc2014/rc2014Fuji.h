#ifdef BUILD_RC2014
#ifndef RC2014FUJI_H
#define RC2014FUJI_H

#include "fujiDevice.h"

#include <cstdint>

#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/md5.h"

#include "network.h"
#include "disk.h"

class rc2014Fuji : public fujiDevice
{
private:
    bool isReady = false;
    bool alreadyRunning = false;
    bool scanStarted = false;
    bool setSSIDStarted = false;

    uint8_t response[1024];
    uint16_t response_len;

    uint8_t bootMode = 0;

    mbedtls_md5_context _md5;
    mbedtls_sha1_context _sha1;
    mbedtls_sha256_context _sha256;
    mbedtls_sha512_context _sha512;

    Hash::Algorithm algorithm = Hash::Algorithm::UNKNOWN;

protected:
    void transaction_continue(transState_t expectMoreData) override {
        rc2014_send_ack();
    }
    void transaction_complete() override {
        rc2014_send_complete();
    }
    void transaction_error() override {
        rc2014_send_error();
    }
    success_is_true transaction_get(void *data, size_t len) override {
        rc2014_recv_buffer((uint8_t *)data, len);
        rc2014_send_ack();
        return success_is_true(true);
    }
    void transaction_put(const void *data, size_t len, bool err) override {
        rc2014_send_buffer((const uint8_t *)data, len);
        rc2014_flush();
        if (err) rc2014_send_error(); else rc2014_send_complete();
    }

    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void rc2014_reset_fujinet();          // 0xFF
    void rc2014_net_get_ssid();           // 0xFE
    void rc2014_net_scan_networks();      // 0xFD
    void rc2014_net_scan_result();        // 0xFC
    void rc2014_net_set_ssid();           // 0xFB
    void rc2014_net_get_wifi_status();    // 0xFA
    void rc2014_mount_host();             // 0xF9
    void rc2014_disk_image_mount();       // 0xF8
    void rc2014_open_directory();         // 0xF7
    void rc2014_read_directory_entry();   // 0xF6
    void rc2014_close_directory();        // 0xF5
    void rc2014_read_host_slots();        // 0xF4
    void rc2014_write_host_slots();       // 0xF3
    void rc2014_read_device_slots();      // 0xF2
    void rc2014_write_device_slots();     // 0xF1
    void rc2014_disk_image_umount();      // 0xE9
    void rc2014_get_adapter_config();     // 0xE8
    void rc2014_new_disk();               // 0xE7
    void rc2014_unmount_host();           // 0xE6
    void rc2014_get_directory_position(); // 0xE5
    void rc2014_set_directory_position(); // 0xE4
    void rc2014_set_hrc2014_index();      // 0xE3
    void rc2014_set_device_filename();    // 0xE2
    void rc2014_set_host_prefix();        // 0xE1
    void rc2014_get_host_prefix();        // 0xE0
    void rc2014_set_rc2014_external_clock(); // 0xDF
    void rc2014_write_app_key();          // 0xDE
    void rc2014_read_app_key();           // 0xDD
    void rc2014_open_app_key();           // 0xDC
    void rc2014_close_app_key();          // 0xDB
    void rc2014_get_device_filename();    // 0xDA
    void rc2014_set_boot_config();        // 0xD9
    void rc2014_copy_file();              // 0xD8
    void rc2014_set_boot_mode();          // 0xD6
    void rc2014_enable_device();          // 0xD5
    void rc2014_disable_device();         // 0xD4
    void rc2014_device_enabled_status();  // 0xD1
    void rc2014_base64_encode_input();    // 0xD0
    void rc2014_base64_encode_compute();  // 0xCF
    void rc2014_base64_encode_length();   // 0xCE
    void rc2014_base64_encode_output();   // 0xCD
    void rc2014_base64_decode_input();    // 0xCC
    void rc2014_base64_decode_compute();  // 0xCB
    void rc2014_base64_decode_length();   // 0xCA
    void rc2014_base64_decode_output();   // 0xC9
    void rc2014_hash_input();             // 0xC8
    void rc2014_hash_compute(bool clear_data); // 0xC7, 0xC3
    void rc2014_hash_length();            // 0xC6
    void rc2014_hash_output();            // 0xC5
    void rc2014_hash_clear();             // 0xC2

    // TODO
    // void rc2014_get_adapter_config_extended(); // 0xC4

    void rc2014_test_command();

    void rc2014_control_status() override;
    void rc2014_control_send();
    void rc2014_control_clr();

    void rc2014_process(uint32_t commanddata, uint8_t checksum) override;

    void shutdown() override;

public:
    bool status_wait_enabled = true;

    rc2014Network *network();

    void debug_tape();

    void insert_boot_device(uint8_t d);

    void setup() override;

    void image_rotate();

    rc2014Fuji();
};

#endif // RC2014FUJI_H
#endif /* BUILD_RC2014 */
