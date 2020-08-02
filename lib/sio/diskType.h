#ifndef _DISKTYPE_
#define _DISKTYPE_

#include <stdio.h>

enum disktype_t {
    DISKTYPE_UNKNOWN = 0,
    DISKTYPE_ATR,
    DISKTYPE_XEX,
    DISKTYPE_COUNT
};

class DiskType
{
public:
    virtual void mount(FILE *f) = 0;
    virtual void umount() = 0;
    virtual bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors) = 0;
};

#endif // _DISKTYPE_
