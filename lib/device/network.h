#ifndef DEVICE_NETWORK_H
#define DEVICE_NETWORK_H

#ifdef BUILD_ATARI
# include "sio/network.h"
#endif

#ifdef BUILD_CBM
# include "iec/network.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/network.h"
#endif

#ifdef NEW_TARGET
# include "new/network.h"
#endif

#endif // DEVICE_NETWORK_H