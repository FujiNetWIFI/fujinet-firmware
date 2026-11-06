#ifndef _BLANK_IMAGE_H
#define _BLANK_IMAGE_H

#include <stdint.h>
#include "fnio.h"
#include "global_types.h"

// The images NEW DISK writes for a Commodore drive
class BlankImage
{
public:
    // A .d64, .d71 or .d81 name gets that drive's formatted image at its standard size,
    // whatever the request asks for: every block free, an empty directory, the file name as
    // disk name. Any other name gets sectorSize * numSectors zeros.
    static success_is_true create(fnFile *f, const char *filename, uint16_t sectorSize,
                                  uint16_t numSectors);
};

#endif // _BLANK_IMAGE_H
