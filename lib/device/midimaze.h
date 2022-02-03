#ifndef DEVICE_MIDIMAZE_H
#define DEVICE_FUJI_H

#ifdef BUILD_ATARI
# include "sio/midimaze.h"
#endif

#ifdef BUILD_CBM
# include "iec/midimaze.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/midimaze.h"
#endif

#ifdef NEW_TARGET
# include "new/midimaze.h"
#endif

#endif // DEVICE_MIDIMAZE_H