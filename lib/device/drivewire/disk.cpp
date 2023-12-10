#ifdef BUILD_COCO

#include "disk.h"

#include <cstring>

#include "../../include/debug.h"

#include "fuji.h"
#include "utils.h"

// External ref to fuji object.
extern drivewireFuji theFuji;

drivewireDisk::drivewireDisk()
{
    device_active = false;
}

// Destructor
drivewireDisk::~drivewireDisk()
{
}

mediatype_t drivewireDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    mediatype_t mt = MEDIATYPE_UNKNOWN;

    Debug_printf("DW disk MOUNT %s\n", filename);

    // Destroy any existing MediaType
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_DSK:
        _media = new MediaTypeDSK();
        _media->_media_host = host;
        strcpy(_media->_disk_filename,filename);
        mt = _media->mount(f, disksize);
        device_active = true;
        break;
    default:
        device_active = false;
        break;
    }

    return mt; 
}
    
void drivewireDisk::unmount()
{
}

bool drivewireDisk::read(uint32_t lsn, uint8_t *buf)
{
    if (!buf)
    {
        Debug_printv("BUFFER is NULL, IGNORED.");
        return true;
    }

    bool r = _media->read(lsn,0);
    memcpy(buf,_media->_media_blockbuff,MEDIA_BLOCK_SIZE);

    return 0;
}

bool drivewireDisk::write(uint32_t lsn, uint8_t *buf)
{
    if (!buf)
    {
        Debug_printv("BUFFER is NULL, IGNORED.");
        return true;
    }

    memcpy(_media->_media_blockbuff,buf,MEDIA_BLOCK_SIZE);
    bool r = _media->write(lsn,0);

    return r;
}

#endif /* BUILD_COCO */