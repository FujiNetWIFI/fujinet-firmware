#ifdef BUILD_RS232 // temporary

#include "diskTypeROM.h"

#include <cstring>

#include "../../include/debug.h"

error_is_true MediaTypeROM::read(uint32_t sectornum, uint32_t *readcount)
{
    Debug_print("ROM READ not supported\r\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::write(uint32_t sectornum, bool verify)
{
    Debug_print("ROM WRITE not supported\r\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::format(uint32_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

void MediaTypeROM::status(uint8_t statusbuff[4])
{
    memset(statusbuff, 0, 4);
}

mediatype_t MediaTypeROM::mount(fnFile *f, uint32_t disksize)
{
    Debug_printv("MediaTypeROM MOUNT (%lu bytes)\n", (unsigned long)disksize);

    _disk_fileh = f;
    _disk_image_size = disksize;
    _disktype = MEDIATYPE_ROM;

    return _disktype;
}

#endif // BUILD_RS232
