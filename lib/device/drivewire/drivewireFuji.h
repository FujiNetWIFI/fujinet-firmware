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

    void shutdown() override;

public:
    drivewireNetwork *network();

#ifdef ESP_PLATFORM
    drivewireCassette *cassette() { return &_cassetteDev; };
#endif
    void debug_tape();

    void setup() override;
    bool processCommand(const FujiDWPacket &packet) override;
    drivewireFuji();

    // ============ Wrapped Fuji commands ============
    std::optional<std::vector<uint8_t>> fujicore_read_app_key() override;
    void fujicmd_open_app_key() override;
    success_is_true fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                      disk_access_flags_t access_mode) override;

};

extern drivewireFuji platformFuji;

#endif // DRIVEWIREFUJI_H
