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
# include "sio/sioNetwork.h"
#endif

#ifdef BUILD_RS232
# include "rs232/rs232Network.h"
#endif

#ifdef BUILD_IEC
# include "iec/iecNetwork.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/adamNetwork.h"
#endif

#ifdef BUILD_LYNX
# include "comlynx/lynxNetwork.h"
#endif

#ifdef BUILD_APPLE
# include "iwm/iwmNetwork.h"
#endif

#ifdef BUILD_S100
# include "s100spi/s100spiNetwork.h"
#endif

#ifdef BUILD_RC2014
# include "rc2014/rc2014Network.h"
#endif

#ifdef BUILD_H89
# include "h89/H89Network.h"
#endif

#ifdef BUILD_COCO
# include "drivewire/drivewireNetwork.h"
#endif

#ifdef NEW_TARGET
# include "new/network.h"
#endif

#endif // DEVICE_NETWORK_H
