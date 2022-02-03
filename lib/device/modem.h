#ifndef DEVICE_MODEM_H
#define DEVICE_MODEM_H

#ifdef BUILD_ATARI
# include "sio/modem.h"
  extern sioModem *sioR;
#endif

#ifdef BUILD_CBM
# include "iec/modem.h"
  extern iecModem *sioR;
#endif

#ifdef BUILD_ADAM
# include "adamnet/modem.h"
  extern adamModem *sioR;
#endif

#ifdef NEW_TARGET
# include "new/modem.h"
extern adamModem *sioR;
#endif

#ifdef BUILD_APPLE
# include "iwm/modem.h"
  extern appleModem *sioR;
#endif


#endif // DEVICE_MODEM_H