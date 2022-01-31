#ifndef DEVICE_PRINTER_H
#define DEVICE_PRINTER_H

#ifdef BUILD_ATARI
#include "sio/printer.h"
#include "sio/printerlist.h"
#define PRINTER_CLASS sioPrinter
#endif

#ifdef BUILD_CBM
#include "iec/printer.h"
#include "iec/printerlist.h"
#define PRINTER_CLASS iecPrinter
#endif

#ifdef BUILD_ADAM
#include "adamnet/printer.h"
#include "adamnet/printerlist.h"
#define PRINTER_CLASS adamPrinter
#endif

#ifdef NEW_TARGET
#include ".new/printer.h"
#include ".new/printerlist.h"
#define PRINTER_CLASS adamPrinter
#endif

#endif // DEVICE_PRINTER_H