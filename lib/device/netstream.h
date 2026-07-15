#ifndef DEVICE_NETSTREAM_H
#define DEVICE_NETSTREAM_H

#ifdef BUILD_ATARI
# include "sio/netstream.h"
#endif

#ifdef BUILD_RS232
#endif

#ifdef BUILD_LYNX
# include "comlynx/netstream.h"
#endif

#ifdef NEW_TARGET
# include "new/netstream.h"
#endif

#endif // DEVICE_NETSTREAM_H