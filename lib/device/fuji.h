#ifndef DEVICE_FUJI_H
#define DEVICE_FUJI_H

#ifdef BUILD_ATARI
#include "sio/fuji.h"

#elif BUILD_CBM
#include "iec/fuji.h"

#elif BUILD_ADAM
#include "adamnet/fuji.h"

#elif NEW_TARGET
#include ".new/fuji.h"
#endif



#endif // DEVICE_FUJI_H