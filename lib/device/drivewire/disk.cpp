#ifdef BUILD_COCO

#include "disk.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "bus.h"
#include "fujiDevice.h"
#include "utils.h"

drivewireDisk::drivewireDisk()
{
    device_active = false;
}

drivewireDisk::~drivewireDisk()
{
    unmount();
}

// A format that accepts the file takes ownership of the handle.
error_is_true drivewireDisk::mount(fnFile *f, const char *filename, uint32_t /*disksize*/,
                                   disk_access_flags_t access_mode, fujiHost *host)
{
    Debug_printf("DW disk MOUNT %s\n", filename);

    unmount();

    std::string path = filename != nullptr ? filename : "";
    bool readOnly = !(access_mode & DISK_ACCESS_MODE_WRITE);
    _media = Formats::open<BlockMedia>(path, f, host, readOnly);
    if (!_media)
    {
        fnio::fclose(f);
        RETURN_ERROR_AS_TRUE();
    }

    _status = DISK_STATUS_OK;
    device_active = true;
    RETURN_SUCCESS_AS_FALSE();
}

void drivewireDisk::unmount()
{
    Debug_print("DW disk UNMOUNT\n");

    if (_media)
    {
        _media->close();
        _media.reset();
    }
    device_active = false;
}

error_is_true drivewireDisk::read(uint32_t sector, ByteBuffer &data)
{
    if (!isMounted())
        RETURN_ERROR_AS_TRUE();

    // A block the media doesn't have is reported to the CoCo as EOF.
    if (_media->blockSize(sector) == 0)
    {
        _status = DISK_STATUS_EOF;
        RETURN_ERROR_AS_TRUE();
    }
    _status = DISK_STATUS_OK;

    return _media->readBlock(sector, data);
}

error_is_true drivewireDisk::write(uint32_t sector, const ByteBuffer &data)
{
    if (!isMounted())
        RETURN_ERROR_AS_TRUE();

    if (_media->blockSize(sector) == 0 || _media->writeBlock(sector, data).is_error())
    {
        _status = DISK_STATUS_EOF;
        RETURN_ERROR_AS_TRUE();
    }
    _status = DISK_STATUS_OK;
    RETURN_SUCCESS_AS_FALSE();
}

uint8_t drivewireDisk::get_media_status()
{
    return isMounted() ? _status : DISK_STATUS_NO_MEDIA;
}

success_is_true drivewireDisk::write_blank(fnFile *f, const NewDiskRequest &req)
{
    uint8_t b[512];
    size_t n = req.numDisks * 315;

    Debug_printf("write_blank num_disks: %u\n", n);

    memset(b,0xFF,sizeof(b));

    for (size_t i=0;i<n;i++)
        fnio::fwrite(b,sizeof(b),1,f);

    RETURN_SUCCESS_AS_TRUE();
}


#endif /* BUILD_COCO */
