#ifndef DEVICE_FUJI_H
#define DEVICE_FUJI_H

#ifdef BUILD_ATARI
# include "sio/fuji.h"
#endif

#ifdef BUILD_RS232
# include "rs232/fuji.h"
#endif

#ifdef BUILD_IEC
# include "iec/fuji.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/fuji.h"
#endif

#ifdef BUILD_LYNX
# include "comlynx/fuji.h"
#endif

#ifdef BUILD_S100
# include "s100spi/fuji.h"
#endif

#ifdef NEW_TARGET
# include "new/fuji.h"
#endif

#ifdef BUILD_APPLE
# include "iwm/fuji.h"
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/fuji.h"
#endif

#endif // DEVICE_FUJI_H