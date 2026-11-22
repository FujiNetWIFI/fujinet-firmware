#ifndef RS232_MEDIATYPE_H
#define RS232_MEDIATYPE_H

#include <cstdint>

// The mediatype_t IDs a directory entry reports. Wire-visible, so append only.
enum mediatype_t : uint8_t
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_IMG,
    MEDIATYPE_ROM,
    MEDIATYPE_IMD,
    MEDIATYPE_ALTAIR,
    MEDIATYPE_COUNT
};

#endif /* RS232_MEDIATYPE_H */
