#ifndef DEVICE_CPM_H
#define DEVICE_CPM_H

#ifdef BUILD_ATARI
# include "sio/siocpm.h"
#endif

#ifdef BUILD_RS232
# include "rs232/rs232cpm.h"
#endif

#ifdef BUILD_CBM
# include "iec/cpm.h"
#endif

#ifdef BUILD_APPLE
# include "iwm/cpm.h"
#endif

#ifdef NEW_TARGET
# include "new/cpm.h"
#endif

#endif // DEVICE_CPM_H