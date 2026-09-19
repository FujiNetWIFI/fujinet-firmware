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
    _image_pos = UINT32_MAX;

    return _mediatype;
}

uint32_t MediaTypeROM::stream_size()
{
    return _media_image_size;
}

size_t MediaTypeROM::stream_read(uint32_t offset, uint8_t *buffer, size_t length)
{
    if (_media_fileh == nullptr || offset >= _media_image_size)
        return 0;

    if (length > _media_image_size - offset)
        length = _media_image_size - offset;

    if (_image_pos != offset)
    {
        if (fnio::fseek(_media_fileh, offset, SEEK_SET) != 0)
            return 0;
        _image_pos = offset;
    }

    size_t got = fnio::fread(buffer, 1, length, _media_fileh);
    _image_pos += got;

    return got;
}

#endif // BUILD_COCO
