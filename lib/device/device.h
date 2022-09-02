#ifndef DEVICE_H
#define DEVICE_H

#ifdef BUILD_ATARI
# include "sio/apetime.h"
# include "sio/cassette.h"
# include "sio/disk.h"
# include "sio/udpstream.h"
# include "sio/modem.h"
# include "sio/network.h"
# include "sio/printer.h"
# include "sio/printerlist.h"
# include "sio/siocpm.h"
# include "sio/voice.h"
# include "sio/fuji.h"

    sioApeTime apeTime;
    sioVoice sioV;
    sioUDPStream udpDev;
    // sioCassette sioC; // now part of sioFuji theFuji object
    sioModem *sioR;
    sioCPM sioZ;
#endif

#ifdef BUILD_RS232
# include "rs232/apetime.h"
# include "rs232/disk.h"
# include "rs232/udpstream.h"
# include "rs232/modem.h"
# include "rs232/network.h"
# include "rs232/printer.h"
# include "rs232/printerlist.h"
# include "rs232/rs232cpm.h"
# include "rs232/fuji.h"

    rs232ApeTime apeTime;
    rs232UDPStream udpDev;
    rs232Modem *sioR;
    rs232CPM sioZ;
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

#ifdef BUILD_LYNX
# include "comlynx/keyboard.h"
# include "comlynx/modem.h"
# include "comlynx/printer.h"
# include "comlynx/printerlist.h"
# include "comlynx/fuji.h"
# include "comlynx/udpstream.h"

//# define NO_VIRTUAL_KEYBOARD
    lynxModem *sioR;
    lynxKeyboard *sioK;
    lynxUDPStream *udpDev;
    bool exists = false;
#endif

#ifdef BUILD_APPLE
# include "iwm/disk.h"
# include "iwm/fuji.h"
# include "iwm/modem.h"
# include "iwm/printer.h"
# include "iwm/printerlist.h"
    appleModem *sioR;
#endif

#ifdef BUILD_S100
# include "s100spi/disk.h"
# include "s100spi/network.h"
# include "s100spi/modem.h"
# include "s100spi/printer.h"
# include "s100spi/printerlist.h"
# include "s100spi/fuji.h"
    s100spiModem *sioR;
#endif

#ifdef NEW_TARGET
# include "new/keyboard.h"
# include "new/modem.h"
# include "new/printer.h"
# include "new/printerlist.h"
# include "new/query_device.h"
# include "new/fuji.h"

//# define NO_VIRTUAL_KEYBOARD
    adamModem *sioR;
    adamKeyboard *sioK;
    adamQueryDevice *sioQ;
    bool exists = false;
#endif

#endif // DEVICE_H