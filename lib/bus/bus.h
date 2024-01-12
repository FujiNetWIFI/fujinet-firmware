#ifndef BUS_H
#define BUS_H

#ifdef BUILD_ATARI
#include "sio/sio.h"
#define SYSTEM_BUS SIO
#endif

#ifdef BUILD_IEC
#include "iec/iec.h"
#include "iec/parallel.h"
#include "iec/ieee-488.h"
#define SYSTEM_BUS IEC
#endif

#ifdef BUILD_ADAM
#include "adamnet/adamnet.h"
#define SYSTEM_BUS AdamNet
#endif

#ifdef BUILD_LYNX
#include "comlynx/comlynx.h"
#define SYSTEM_BUS ComLynx
#endif

#ifdef NEW_TARGET
#include "new/adamnet.h"
#define SYSTEM_BUS AdamNet
#endif

#ifdef BUILD_APPLE
#include "iwm/iwm.h"
#define SYSTEM_BUS IWM
#endif

#ifdef BUILD_MAC
#include "mac/mac.h"
#define SYSTEM_BUS MAC
#endif

#ifdef BUILD_S100
#include "s100spi/s100spi.h"
#define SYSTEM_BUS s100Bus
#endif

#ifdef BUILD_RS232
#include "rs232/rs232.h"
#define SYSTEM_BUS RS232
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/cx16_i2c.h"
#define SYSTEM_BUS CX16
#endif

#ifdef BUILD_RC2014
#include "rc2014bus/rc2014bus.h"
#define SYSTEM_BUS rc2014Bus
#endif

#ifdef BUILD_H89
#include "h89/h89.h"
#define SYSTEM_BUS H89Bus
#endif

#ifdef BUILD_COCO
#include "drivewire/drivewire.h"
#define SYSTEM_BUS DRIVEWIRE
#endif

#endif // BUS_H
