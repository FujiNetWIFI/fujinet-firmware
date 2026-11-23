#ifndef DRIVEWIRE_MEDIATYPE_H
#define DRIVEWIRE_MEDIATYPE_H

#include <cstdint>

// The mediatype_t IDs a directory entry reports, numbered as the client expects them.
enum mediatype_t : uint8_t
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_DSK,
    MEDIATYPE_MRM,
    MEDIATYPE_VDK,
    MEDIATYPE_ROM,
    MEDIATYPE_CAS,
    MEDIATYPE_COUNT
};

#endif /* DRIVEWIRE_MEDIATYPE_H */
