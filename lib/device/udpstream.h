#ifndef DEVICE_UDPSTREAM_H
#define DEVICE_UDPSTREAM_H

#ifdef BUILD_ATARI
# include "sio/udpstream.h"
#endif

#ifdef BUILD_CBM
# include "iec/udpstream.h"
#endif

#ifdef BUILD_LYNX
# include "comlynx/udpstream.h"
#endif

#ifdef NEW_TARGET
# include "new/udpstream.h"
#endif

#endif // DEVICE_UDPSTREAM_H