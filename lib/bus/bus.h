#ifndef BUS_H
#define BUS_H

#ifdef BUILD_ATARI
#include "sio/sio.h"
#define SYSTEM_BUS SIO
#endif

#ifdef BUILD_CBM
#include "iec/iec.h"
#define SYSTEM_BUS IEC
#endif

#ifdef BUILD_ADAM
#include "adamnet/adamnet.h"
#define SYSTEM_BUS AdamNet
#endif

#ifdef NEW_TARGET
#include "new/adamnet.h"
#define SYSTEM_BUS AdamNet
#endif

#ifdef BUILD_APPLE
#include "iwm/iwm.h"
#define SYSTEM_BUS IWM
#endif

#ifdef BUILD_S100
#include "s100spi/s100spi.h"
#define SYSTEM_BUS s100Bus
#endif

#endif // BUS_H