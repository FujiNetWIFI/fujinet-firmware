#ifndef DEVICE_H
#define DEVICE_H

#ifdef BUILD_ATARI
# include "sio/apetime.h"
# include "sio/cassette.h"
# include "sio/disk.h"
# include "sio/midimaze.h"
# include "sio/modem.h"
# include "sio/network.h"
# include "sio/printer.h"
# include "sio/printerlist.h"
# include "sio/siocpm.h"
# include "sio/voice.h"
# include "sio/fuji.h"

    sioApeTime apeTime;
    sioVoice sioV;
    sioMIDIMaze sioMIDI;
    // sioCassette sioC; // now part of sioFuji theFuji object
    sioModem *sioR;
    sioCPM sioZ;
#endif

#ifdef BUILD_CBM
# include "iec/printer.h"
# include "iec/printerlist.h"
# include "iec/fuji.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/keyboard.h"
# include "adamnet/modem.h"
# include "adamnet/printer.h"
# include "adamnet/printerlist.h"
# include "adamnet/query_device.h"
# include "adamnet/fuji.h"

//# define NO_VIRTUAL_KEYBOARD
    adamModem *sioR;
    adamKeyboard *sioK;
    adamQueryDevice *sioQ;
    bool exists = false;
#endif

#ifdef NEW_TARGET
# include ".new/keyboard.h"
# include ".new/modem.h"
# include ".new/printer.h"
# include ".new/printerlist.h"
# include ".new/query_device.h"
# include ".new/fuji.h"

//# define NO_VIRTUAL_KEYBOARD
    adamModem *sioR;
    adamKeyboard *sioK;
    adamQueryDevice *sioQ;
    bool exists = false;
#endif

#endif // DEVICE_H