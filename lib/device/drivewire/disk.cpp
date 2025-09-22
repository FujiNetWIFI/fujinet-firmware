#ifdef BUILD_COCO

#include "disk.h"

#include <cstring>

#include "../../include/debug.h"

#include "drivewireFuji.h"
#include "utils.h"

drivewireDisk::drivewireDisk()
{
    device_active = false;
}

// Destructor
drivewireDisk::~drivewireDisk()
{
}

mediatype_t drivewireDisk::mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
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
        break;
    case MEDIATYPE_MRM:
        _media = new MediaTypeMRM();
        break;
    case MEDIATYPE_VDK:
        _media = new MediaTypeVDK();
        break;    
    default:
        device_active = false;
        break;
    }

    if (_media)
    {
        _media->_media_host = host;
        strcpy(_media->_disk_filename,filename);
        mt = _media->mount(f, disksize);
        device_active = true;
    }

    return mt; 
}
    
void drivewireDisk::unmount()
{
}

bool drivewireDisk::read(uint32_t lsn, uint8_t *buf)
{
    bool r = _media->read(lsn,0);
    // copy data to destination buffer, if provided
    if (buf)
    {
        memcpy(buf, _media->_media_blockbuff, MEDIA_BLOCK_SIZE);
    }
    return r;
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

void drivewireDisk::get_media_buffer(uint8_t **p_buffer, uint16_t *p_blk_size)
{
    if (_media)
    {
        // query media for block buffer and block size
        _media->get_block_buffer(p_buffer, p_blk_size);
    }
    else
    {
        *p_buffer = nullptr;
        *p_blk_size = 0;
    }
}

uint8_t drivewireDisk::get_media_status()
{
    if (!_media)
        return 3; // ? NO MEDIA
    return _media->status();
}

bool drivewireDisk::write_blank(fnFile *f, uint8_t numDisks)
{
    uint8_t b[512];
    size_t n = numDisks * 315;

    Debug_printf("write_blank num_disks: %u\n", n);

    memset(b,0xFF,sizeof(b));

    for (size_t i=0;i<n;i++)
        fnio::fwrite(b,sizeof(b),1,f);

    return true;
}


#endif /* BUILD_COCO */
