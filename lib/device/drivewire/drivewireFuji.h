#ifndef DRIVEWIREFUJI_H
#define DRIVEWIREFUJI_H

#include "fujiDevice.h"

#include "cassette.h"

#define MAX_DWDISK_DEVICES 4

class drivewireFuji : public fujiDevice
{
private:
#ifdef ESP_PLATFORM
    drivewireCassette _cassetteDev;
#endif

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void new_disk();               // 0xE7
    void random();                 // 0xD3
    void base64_encode_input();    // 0xD0
    void base64_encode_compute();  // 0xCF
    void base64_encode_length();   // 0xCE
    void base64_encode_output();   // 0xCD
    void base64_decode_input();    // 0xCC
    void base64_decode_compute();  // 0xCB
    void base64_decode_length();   // 0xCA
    void base64_decode_output();   // 0xC9
    void hash_input();             // 0xC8
    void hash_compute(bool clear_data); // 0xC7, 0xC3
    void hash_length();            // 0xC6
    void hash_output();            // 0xC5
    void hash_clear();             // 0xC2

    void shutdown() override;

public:
    drivewireNetwork *network();

#ifdef ESP_PLATFORM
    drivewireCassette *cassette() { return &_cassetteDev; };
#endif
    void debug_tape();

    void setup() override;
    void process(fujiCommandID_t cmd);
    drivewireFuji();

    // ============ Wrapped Fuji commands ============
    std::optional<std::vector<uint8_t>> fujicore_read_app_key() override;
    void fujicmd_open_app_key() override;
    success_is_true fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                      disk_access_flags_t access_mode) override;

};

extern drivewireFuji platformFuji;

#endif // DRIVEWIREFUJI_H
