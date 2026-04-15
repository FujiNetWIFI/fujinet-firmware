#ifndef LYNX_DISK_H
#define LYNX_DISK_H

#include "../disk.h"
#include "bus.h"
#include "global_types.h"
#include "media.h"


class lynxDisk : public virtualDevice
{
private:
    MediaType *_media = nullptr;
  
    unsigned long blockNum=INVALID_SECTOR_VALUE;
    
    void comlynx_process() override;
    void read_block(uint32_t block);
    void write_block(uint32_t block);

public:
    lynxDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    error_is_true write_blank(FILE *f, uint32_t numBlocks);
    virtual void reset() override;

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    ~lynxDisk();
};

#endif /* LYNX_DISK_H */
