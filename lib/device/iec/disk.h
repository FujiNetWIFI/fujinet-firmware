#ifndef DISK_H
#define DISK_H

#include <fujiHost.h>
#include <string>
#include "bus.h"
#include "media.h"

class iecDisk : public virtualDevice
{
private:
    MediaType *_disk = nullptr;

    struct _error_response
    {
        unsigned char errnum = 73;
        std::string msg = "CBM DOS V2.6 1541";
        unsigned char track = 0;
        unsigned char sector = 0;
    } error_response;

    void read();
    void write(bool verify);
    void format();
    void status() override;

protected:
    device_state_t process(IECData *commanddata) override;
    void process_load();
    void process_save();
    void process_command();
    void process_file();

public:
    iecDisk();
    fujiHost *host;
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };

    ~iecDisk();
};

#endif
