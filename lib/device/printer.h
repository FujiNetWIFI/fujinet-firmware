#ifndef DEVICE_PRINTER_H
#define DEVICE_PRINTER_H

#ifdef BUILD_ATARI
# include "sio/printer.h"
# include "sio/printerlist.h"
# define PRINTER_CLASS sioPrinter
#endif

#ifdef BUILD_RS232
# include "rs232/printer.h"
# include "rs232/printerlist.h"
# define PRINTER_CLASS rs232Printer
#endif

#ifdef BUILD_IEC
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

#ifdef BUILD_RC2014
# include "rc2014/printer.h"
# include "rc2014/printerlist.h"
# define PRINTER_CLASS rc2014Printer
#endif

#ifdef BUILD_H89
# include "h89/printer.h"
# include "h89/printerlist.h"
# define PRINTER_CLASS H89Printer
#endif

#ifdef NEW_TARGET
# include "new/printer.h"
# include "new/printerlist.h"
# define PRINTER_CLASS adamPrinter
#endif

#ifdef BUILD_APPLE
# include "iwm/printer.h"
# include "iwm/printerlist.h"
# define PRINTER_CLASS iwmPrinter
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/printer.h"
#include "cx16_i2c/printerlist.h"
#define PRINTER_CLASS cx16Printer
#endif

#endif // DEVICE_PRINTER_H