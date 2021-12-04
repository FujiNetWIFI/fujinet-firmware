#ifndef DISK_H
#define DISK_H

#include "bus.h"
#include "media.h"

class smartDisk : public smartDevice
{
private:
    DiskType *_disk = nullptr;

    void smart_read();
    void smart_write(bool verify);
    // void smart_format();
    void smart_status() override;
    void smart_process(uint32_t commanddata, uint8_t checksum) override;

    // void derive_percom_block(uint16_t numSectors);
    // void smart_read_percom_block();
    // void smart_write_percom_block();
    // void dump_percom_block();

public:
    smartDisk();
    disktype_t mount(FILE *f, const char *filename, uint32_t disksize, disktype_t disk_type = DISKTYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    disktype_t disktype() { return _disk == nullptr ? DISKTYPE_UNKNOWN : _disk->_disktype; };

    ~smartDisk();
};

#endif
