#ifndef DEVICE_FUJI_H
#define DEVICE_FUJI_H

#ifdef BUILD_ATARI
# include "sio/sioFuji.h"
#endif

#ifdef BUILD_RS232
# include "rs232/rs232Fuji.h"
#endif

#ifdef BUILD_IEC
# include "iec/iecFuji.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/adamFuji.h"
#endif

#ifdef BUILD_LYNX
# include "comlynx/lynxFuji.h"
#endif

#ifdef BUILD_S100
# include "s100spi/s100spiFuji.h"
#endif

#ifdef NEW_TARGET
# include "new/fuji.h"
#endif

#ifdef BUILD_APPLE
# include "iwm/iwmFuji.h"
#endif

#ifdef BUILD_MAC
# include "mac/macFuji.h"
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/cx16Fuji.h"
#endif

#ifdef BUILD_RC2014
# include "rc2014/rc2014Fuji.h"
#endif

#ifdef BUILD_H89
# include "h89/H89Fuji.h"
#endif

#ifdef BUILD_COCO
# include "drivewire/drivewireFuji.h"
#endif 

#endif // DEVICE_FUJI_H
