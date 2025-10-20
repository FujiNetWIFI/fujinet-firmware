#ifndef BUS_H
#define BUS_H

#ifdef BUILD_ATARI
#include "sio/sio.h"
#ifdef ESP_PLATFORM
  #define FN_BUS_PORT fnUartBUS
#else
  #define FN_BUS_PORT fnSioCom
#endif
#endif

#ifdef BUILD_IEC
#include "iec/iec.h"
#define FN_BUS_PORT fnUartBUS  // TBD
#endif

#ifdef BUILD_ADAM
#include "adamnet/adamnet.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_LYNX
#include "comlynx/comlynx.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef NEW_TARGET
#include "new/adamnet.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_APPLE
#include "iwm/iwm.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_MAC
#include "mac/mac.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_S100
#include "s100spi/s100spi.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_RS232
#include "rs232/rs232.h"
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/cx16_i2c.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_RC2014
#include "rc2014bus/rc2014bus.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_H89
#include "h89/h89.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_COCO
#include "drivewire/drivewire.h"
#define FN_BUS_PORT fnDwCom
#endif

#endif // BUS_H
