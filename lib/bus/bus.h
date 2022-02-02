#ifndef BUS_H
#define BUS_H

#ifdef BUILD_ATARI
# include "sio/sio.h"
# define BUS_CLASS SIO
#endif

#ifdef BUILD_CBM
# include "iec/iec.h"
# define BUS_CLASS IEC
#endif

#ifdef BUILD_ADAM
# include "adamnet/adamnet.h"
# define BUS_CLASS AdamNet
#endif

#ifdef NEW_TARGET
# include "new/adamnet.h"
# define BUS_CLASS AdamNet
#endif

#endif // BUS_H