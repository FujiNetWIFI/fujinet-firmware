#ifdef BUILD_COCO

#include "mediaTypeROM.h"

#include "../../include/debug.h"

error_is_true MediaTypeROM::read(uint32_t blockNum, uint16_t *readcount)
{
    Debug_printf("DW ROM READ not supported\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::write(uint32_t blockNum, bool verify)
{
    Debug_printf("DW ROM WRITE not supported\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::format(uint16_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

uint8_t MediaTypeROM::status()
{
    return 2;
}

mediatype_t MediaTypeROM::mount(fnFile *f, uint32_t disksize)
{
    Debug_printf("DW ROM MOUNT %s (%lu bytes)\n", _disk_filename, (unsigned long)disksize);

    _media_fileh = f;
    _mediatype = MEDIATYPE_ROM;
    _media_image_size = disksize;

    return _mediatype;
}

#endif // BUILD_COCO
