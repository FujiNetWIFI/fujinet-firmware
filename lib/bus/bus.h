#ifndef BUS_H
#define BUS_H

#include "fnSystem.h"

#if defined( BUILD_ATARI )
#   include "sio/sio.h"
#elif defined( BUILD_CBM )
#   include "iec/iec.h"
#endif

#endif // BUS_H