#ifndef DISK_H
#define DISK_H

#include <ctime>

#include "bus.h"
#include "media.h"

// For compatibility with other disks that declare host variable
#include "../../fuji/fujiHost.h"

class rs232Disk : public virtualDevice
{
private:
    MediaType *_disk = nullptr;

    void rs232_read(uint32_t sector);
    void rs232_write(uint32_t sector, bool verify);
    void rs232_format();
    void rs232_status(FujiStatusReq reqType) override;
    void rs232_process(FujiBusCommand& command) override;

    void derive_percom_block(uint16_t numSectors);
    void rs232_read_percom_block();
    void rs232_write_percom_block();
    void dump_percom_block();

public:
    time_t mount_time = 0;
    fujiHost *host = nullptr;

    rs232Disk();
    mediatype_t mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(fnFile *f, uint16_t sectorSize, uint16_t numSectors);

    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_disktype; };

    ~rs232Disk();
};

#endif
