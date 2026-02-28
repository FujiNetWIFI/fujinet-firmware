#ifndef DEVICE_NETWORK_H
#define DEVICE_NETWORK_H

#include "status_error_codes.h"

typedef struct {
    uint16_t avail;
    uint8_t conn;
    nDevStatus_t err;
} NDeviceStatus;
static_assert(sizeof(NDeviceStatus) == 4, "NDeviceStatus must be 4 bytes");

#ifdef BUILD_ATARI
# include "sio/network.h"
#endif

#ifdef BUILD_RS232
# include "rs232/network.h"
#endif

#ifdef BUILD_IEC
# include "iec/network.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/network.h"
#endif

#ifdef BUILD_LYNX
# include "comlynx/network.h"
#endif

#ifdef BUILD_APPLE
# include "iwm/network.h"
#endif

#ifdef BUILD_S100
# include "s100spi/network.h"
#endif

#ifdef BUILD_RC2014
# include "rc2014/network.h"
#endif

#ifdef BUILD_H89
# include "h89/network.h"
#endif

#ifdef BUILD_COCO
# include "drivewire/network.h"
#endif

#ifdef NEW_TARGET
# include "new/network.h"
#endif

#endif // DEVICE_NETWORK_H
