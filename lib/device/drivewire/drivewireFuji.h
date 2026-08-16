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

    void insert_boot_device(uint8_t image_id, mediatype_t disk_type,
                            DISK_DEVICE *disk_dev) override;

    // ============ Wrapped Fuji commands ============
    ByteBuffer appkey_read() override;
    void appkey_write(const FUJI_COMMAND_PACKET &packet) override;

    success_is_true fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                      disk_access_flags_t access_mode) override;

};

extern drivewireFuji platformFuji;

#endif // DRIVEWIREFUJI_H
