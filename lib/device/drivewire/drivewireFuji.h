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

    void new_disk();
    void random();
    void base64_encode_input(uint16_t len);
    void base64_encode_compute();
    void base64_encode_length();
    void base64_encode_output(uint16_t len);
    void base64_decode_input(uint16_t len);
    void base64_decode_compute();
    void base64_decode_length();
    void base64_decode_output(uint16_t len);
    void hash_input(uint16_t len);
    void hash_compute(bool clear_data, uint8_t algo);
    void hash_length(bool is_hex);
    void hash_output(bool is_hex);
    void hash_clear();

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
