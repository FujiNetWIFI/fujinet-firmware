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
    mediatype_t directoryMediaType(const char *filename) override {
        return JoinedMediaFormats<drivewireDisk::Formats>::entryType(filename);
    }

    // Passes `host` through so CoCoDSK can write high scores to a read-only image.
    error_is_true mount_media(DISK_DEVICE *disk_dev, fnFile *fileh, fujiDisk &disk,
                              fujiHost &host, disk_access_flags_t mode) override
    {
        return disk_dev->mount(fileh, disk.filename, disk.disk_size, mode, &host);
    }

    void fujidev_copy_file(const FUJI_COMMAND_PACKET &packet) override;

    void random();

    void shutdown() override;

    // -1 stops the base from blinking before the queued swap runs; rotate_disks() blinks.
    int get_rotate_slot() override { return -1; }

    std::string config_image() override;

public:
    drivewireNetwork *network();

#ifdef ESP_PLATFORM
    drivewireCassette *cassette() { return &_cassetteDev; };
#endif
    void debug_tape();

    void setup() override;
    bool processCommand(const FujiDWPacket &packet) override;
    drivewireFuji();

    void insert_boot_device(uint8_t image_id, DISK_DEVICE *disk_dev) override;

    // ============ Wrapped Fuji commands ============
    ByteBuffer appkey_read() override;
    void appkey_write(const FUJI_COMMAND_PACKET &packet) override;

    // Called by the bus between transfers.
    void rotate_disks();
};

#endif // DRIVEWIREFUJI_H
