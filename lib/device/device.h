#ifndef DEVICE_H
#define DEVICE_H

#ifdef BUILD_ATARI
# include "sio/clock.h"
# include "sio/cassette.h"
# include "sio/disk.h"
# include "sio/pclink.h"
# include "sio/udpstream.h"
# include "sio/modem.h"
# include "sio/network.h"
# include "sio/printer.h"
# include "sio/printerlist.h"
# include "sio/siocpm.h"
# include "sio/voice.h"
# include "sio/sioFuji.h"

    sioClock clockDevice;
    sioVoice sioV;
    sioUDPStream udpDev;
    // sioCassette sioC; // now part of sioFuji theFuji object
    modem *sioR;
    sioCPM sioZ;
    sioPCLink pcLink;
#endif // BUILD_ATARI

#ifdef BUILD_COCO
# include "drivewire/cassette.h"
# include "drivewire/clock.h"
# include "drivewire/disk.h"
# include "drivewire/modem.h"
# include "drivewire/network.h"
# include "drivewire/printer.h"
# include "drivewire/printerlist.h"
# include "drivewire/drivewireFuji.h"

    drivewireClock dwClock;
    drivewireModem *sioR;
#endif

#ifdef BUILD_RS232
# include "rs232/apetime.h"
# include "rs232/disk.h"
# include "rs232/modem.h"
# include "rs232/network.h"
# include "rs232/printer.h"
# include "rs232/printerlist.h"
# include "rs232/rs232cpm.h"
# include "rs232/rs232Fuji.h"

    rs232ApeTime apeTime;
    rs232Modem *sioR;
    rs232CPM sioZ;
#endif

#ifdef BUILD_IEC
# include "iec/clock.h"
# include "iec/cpm.h"
# include "iec/drive.h"
# include "iec/modem.h"
# include "iec/network.h"
# include "iec/printer.h"
# include "iec/printerlist.h"
# include "iec/iecFuji.h"

    iecModem *sioR;
#endif

#ifdef BUILD_ADAM
# include "adamnet/keyboard.h"
# include "adamnet/modem.h"
# include "adamnet/printer.h"
# include "adamnet/printerlist.h"
# include "adamnet/query_device.h"
# include "adamnet/adamFuji.h"

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
# include "comlynx/lynxFuji.h"
# include "comlynx/udpstream.h"

//# define NO_VIRTUAL_KEYBOARD
    lynxModem *sioR;
    lynxKeyboard *sioK;
    lynxUDPStream *udpDev;
    bool exists = false;
#endif

#ifdef BUILD_APPLE
# include "iwm/disk.h"
# include "iwm/iwmFuji.h"
# include "iwm/modem.h"
# include "iwm/printer.h"
# include "iwm/printerlist.h"
    iwmModem *sioR;
#endif

#ifdef BUILD_MAC
#include "mac/floppy.h"
#include "mac/macFuji.h"
#include "mac/modem.h"
#include "mac/printer.h"
#include "mac/printerlist.h"
    macModem *sioR;
#endif

#ifdef BUILD_S100
#include "s100spi/disk.h"
#include "s100spi/network.h"
#include "s100spi/modem.h"
#include "s100spi/printer.h"
#include "s100spi/printerlist.h"
#include "s100spi/s100spiFuji.h"
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

#ifdef BUILD_CX16
# include "cx16_i2c/disk.h"
# include "cx16_i2c/modem.h"
//# include "cx16_i2c/network.h"
# include "cx16_i2c/printer.h"
# include "cx16_i2c/printerlist.h"
# include "cx16_i2c/cx16Fuji.h"

    cx16Modem *sioR;
#endif

#ifdef BUILD_RC2014
# include "rc2014/disk.h"
# include "rc2014/network.h"
# include "rc2014/modem.h"
# include "rc2014/printer.h"
# include "rc2014/printerlist.h"
# include "rc2014/rc2014Fuji.h"
    rc2014Modem *sioR;
#endif

#ifdef BUILD_H89
# include "h89/disk.h"
# include "h89/network.h"
# include "h89/modem.h"
# include "h89/printer.h"
# include "h89/printerlist.h"
# include "h89/H89Fuji.h"
    H89Modem *sioR;
#endif

#endif // DEVICE_H
