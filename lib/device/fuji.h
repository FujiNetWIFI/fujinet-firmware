#ifndef DEVICE_FUJI_H
#define DEVICE_FUJI_H

#ifdef BUILD_ATARI
# include "sio/fuji.h"
#endif

#ifdef BUILD_CBM
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


#endif // DEVICE_FUJI_H