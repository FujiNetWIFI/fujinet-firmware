#ifndef DEVICE_8BITHUB_H
#define DEVICE_8BITHUB_H

#ifdef BUILD_ATARI
# include "sio/8bithub.h"
#endif

#ifdef BUILD_CBM
# include "iec/8bithub.h"
#endif

#ifdef BUILD_LYNX
# include "comlynx/8bithub.h"
#endif

#ifdef NEW_TARGET
# include "new/8bithub.h"
#endif

#endif // DEVICE_8BITHUB_H