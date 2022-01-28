#ifndef DEVICE_MODEM_H
#define DEVICE_MODEM_H

#ifdef BUILD_ATARI
#include "sio/modem.h"
extern sioModem *sioR;

#elif BUILD_CBM
#include "iec/modem.h"
extern iecModem *sioR;

#elif BUILD_ADAM
#include "adamnet/modem.h"
extern adamModem *sioR;

#elif NEW_TARGET
#include ".new/modem.h"
extern adamModem *sioR;
#endif 

#endif // DEVICE_MODEM_H