#ifndef DEVICE_CASSETTE_H
#define DEVICE_CASSETTE_H

#ifdef BUILD_ATARI
# include "sio/cassette.h"
#endif

#ifdef BUILD_CBM
# include "iec/cassette.h"
#endif

#ifdef NEW_TARGET
# include "new/cassette.h"
#endif

#endif // DEVICE_CASSETTE_H