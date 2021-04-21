#ifndef BUS_H
#define BUS_H

#include "fnSystem.h"

union cmdFrame_t
{
    struct
    {
        uint8_t device;
        uint8_t comnd;
        uint8_t aux1;
        uint8_t aux2;
        uint8_t cksum;
    };
    struct
    {
        uint32_t commanddata;
        uint8_t checksum;
    } __attribute__((packed));
};

#if defined( BUILD_ATARI )
#   include "sio/sio.h"
#elif defined( BUILD_CBM )
#   include "iec/iec.h"
#endif

#endif // BUS_H