#ifndef DEVICE_H
#define DEVICE_H

#include "media.h"

#ifdef BUILD_ATARI
# include "sio/fuji.h"
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

extern sioFuji theFuji;
# define BUS SIO
# define PRINTER_CLASS sioPrinter
# define MEDIA_TYPE disktype_t
# define MEDIA_TYPE_UNKNOWN DISKTYPE_UNKNOWN
# define DEVICE_TYPE sioDisk

#elif BUILD_CBM
# include "iec/fuji.h"
# include "iec/disk.h"
# include "iec/printer.h"
# include "iec/printerlist.h"

extern iecFuji theFuji;
# define BUS IEC
# define PRINTER_CLASS iecPrinter
# define MEDIA_TYPE mediatype_t
# define MEDIA_TYPE_UNKNOWN MEDIATYPE_UNKNOWN
# define DEVICE_TYPE iecDisk

#elif BUILD_ADAM
# include "adamnet/fuji.h"
# include "adamnet/disk.h"
# include "adamnet/keyboard.h"
# include "adamnet/printer.h"
# include "adamnet/modem.h"
# include "adamnet/printerlist.h"
# include "adamnet/query_device.h"

extern adamFuji theFuji;
# define BUS AdamNet
# define PRINTER_CLASS adamPrinter
# define MEDIA_TYPE mediatype_t
# define MEDIA_TYPE_UNKNOWN MEDIATYPE_UNKNOWN
# define DEVICE_TYPE adamDisk

#endif

#endif // DEVICE_H