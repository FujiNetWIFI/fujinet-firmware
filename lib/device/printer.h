#ifndef DEVICE_PRINTER_H
#define DEVICE_PRINTER_H

#ifdef BUILD_ATARI
#include "sio/printer.h"
#include "sio/printerlist.h"
#define PRINTER_CLASS sioPrinter

#elif BUILD_CBM
#include "iec/printer.h"
#include "iec/printerlist.h"
#define PRINTER_CLASS iecPrinter

#elif BUILD_ADAM
#include "adamnet/printer.h"
#include "adamnet/printerlist.h"
#define PRINTER_CLASS adamPrinter

#elif NEW_TARGET
#include ".new/printer.h"
#include "adamnet/printerlist.h"
#define PRINTER_CLASS adamPrinter
#endif

#endif // DEVICE_PRINTER_H