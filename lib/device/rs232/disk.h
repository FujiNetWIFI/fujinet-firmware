#ifndef DISK_H
#define DISK_H

#include <ctime>

#include "../disk.h"
#include "bus.h"
#include "media.h"

class rs232Disk : public virtualDevice
{
private:
    MediaType *_disk = nullptr;
    time_t _mount_time = 0;

    void rs232_read(uint32_t sector);
    void rs232_write(uint32_t sector, bool verify);
    void rs232_format();
    void rs232_status(FujiStatusReq reqType) override;
    void rs232_process(FujiBusPacket &packet) override;

    void derive_percom_block(uint16_t numSectors);
    void rs232_read_percom_block();
    void rs232_write_percom_block();
    void dump_percom_block();

public:
    rs232Disk();
    mediatype_t mount(fnFile *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    mediatype_t mountROM(fnFile *f, const char *filename, uint32_t disksize,
                         mediatype_t disk_type);
    void unmount();
    bool write_blank(fnFile *f, uint16_t sectorSize, uint16_t numSectors);

    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_disktype; };
    time_t mount_time() { return _mount_time; }

    ~rs232Disk();
};

#endif
