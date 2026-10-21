#ifndef DISK_H
#define DISK_H

#include <ctime>

#include "../disk.h"
#include "bus.h"
#include "media.h"

class fujiHost;

class rs232Disk : public virtualDevice
{
private:
    MediaType *_disk = nullptr;
    time_t _mount_time = 0;

    void rs232_read(uint32_t sector);
    void rs232_write(uint32_t sector, bool verify);
    void rs232_format();
    void rs232_status(FujiStatusReq reqType);
    void rs232_process(const FujiBusPacket &packet) override;

    void derive_percom_block(uint16_t numSectors);
    void rs232_read_percom_block();
    void rs232_write_percom_block();
    void dump_percom_block();

public:
    rs232Disk();
    mediatype_t mount(fnFile *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN,
                      fujiHost *host = nullptr);
    mediatype_t mount_disk_media(fnFile *f, const char *filename, uint32_t disksize,
                                 mediatype_t disk_type);
    void unmount();
    struct NewDiskRequest {
        u16le_t numSectors;
        u16le_t sectorSize;
        uint8_t hostSlot;
        uint8_t deviceSlot;
        char filename[MAX_FILENAME_LEN];
    } __attribute__((packed));
    success_is_true write_blank(fnFile *f, const NewDiskRequest &req);

    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_disktype; };
    MediaTypeROM *rom()
    {
        return disktype() == MEDIATYPE_ROM ? static_cast<MediaTypeROM *>(_disk) : nullptr;
    }
    time_t mount_time() { return _mount_time; }

    ~rs232Disk();
};

#endif
