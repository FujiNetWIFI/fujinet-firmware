#ifndef DEVICE_MODEM_H
#define DEVICE_MODEM_H

#ifdef BUILD_ATARI
# include "../modem/modem.h"
  extern modem *sioR;
#endif

#ifdef BUILD_RS232
# include "rs232/modem.h"
  extern rs232Modem *sioR;
#endif

#ifdef BUILD_CBM
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
  extern appleModem *sioR;
#endif


#endif // DEVICE_MODEM_H