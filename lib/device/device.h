#ifndef DEVICE_H
#define DEVICE_H

#if defined( BUILD_ATARI )
#   include "sio/fuji.h"
#   include "sio/apetime.h"
#   include "sio/cassette.h"
#   include "sio/disk.h"
#   include "sio/midimaze.h"
#   include "sio/modem.h"
#   include "sio/network.h"
#   include "sio/printer.h"
#   include "sio/printerlist.h"
#   include "sio/siocpm.h"
#   include "sio/voice.h"
#elif defined( BUILD_CBM )
#   include "iec/fuji.h"
#   include "iec/disk.h"
#elif defined( BUILD_ADAM )
#   include "adamnet/adamnet.h"
#endif

#endif // DEVICE_H