#ifndef DEVICE_PRINTER_H
#define DEVICE_PRINTER_H

#ifdef BUILD_ATARI
# include "sio/printer.h"
# include "sio/printerlist.h"
# define PRINTER_CLASS sioPrinter
#endif

#ifdef BUILD_CBM
# include "iec/printer.h"
# include "iec/printerlist.h"
# define PRINTER_CLASS iecPrinter
#endif

#ifdef BUILD_ADAM
# include "adamnet/printer.h"
# include "adamnet/printerlist.h"
# define PRINTER_CLASS adamPrinter
#endif

#ifdef BUILD_LYNX
# include "comlynx/printer.h"
# include "comlynx/printerlist.h"
# define PRINTER_CLASS lynxPrinter
#endif

#ifdef BUILD_S100
# include "s100spi/printer.h"
# include "s100spi/printerlist.h"
# define PRINTER_CLASS s100spiPrinter
#endif

#ifdef NEW_TARGET
# include "new/printer.h"
# include "new/printerlist.h"
# define PRINTER_CLASS adamPrinter
#endif

#ifdef BUILD_APPLE
# include "iwm/printer.h"
# include "iwm/printerlist.h"
# define PRINTER_CLASS applePrinter
#endif

#endif // DEVICE_PRINTER_H