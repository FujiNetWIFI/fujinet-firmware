#ifndef DEVICE_MODEM_H
#define DEVICE_MODEM_H

#ifdef BUILD_ATARI
# include "sio/modem.h"
  extern modem *sioR;
#endif

#ifdef BUILD_RS232
# include "rs232/modem.h"
  extern rs232Modem *sioR;
#endif

#ifdef BUILD_IEC
# include "iec/modem.h"
  extern iecModem *sioR;
#endif

#ifdef BUILD_ADAM
# include "adamnet/modem.h"
  extern adamModem *sioR;
#endif

#ifdef BUILD_LYNX
# include "comlynx/modem.h"
  extern lynxModem *sioR;
#endif

#ifdef BUILD_S100
# include "s100spi/modem.h"
  extern s100spiModem *sioR;
#endif

#ifdef NEW_TARGET
# include "new/modem.h"
extern adamModem *sioR;
#endif

#ifdef BUILD_APPLE
# include "iwm/modem.h"
  extern iwmModem *sioR;
#endif

#ifdef BUILD_MAC
#include "mac/modem.h"
  extern macModem *sioR;
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/modem.h"
  extern cx16Modem *sioR;
#endif

#ifdef BUILD_RC2014
#include "rc2014/modem.h"
  extern rc2014Modem *sioR;
#endif

#ifdef BUILD_H89
# include "h89/modem.h"
  extern H89Modem *sioR;
#endif

#ifdef BUILD_COCO
# include "drivewire/modem.h"
  extern drivewireModem *sioR;
#endif

#endif // DEVICE_MODEM_H
