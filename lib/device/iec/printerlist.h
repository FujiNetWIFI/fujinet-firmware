#ifndef DEVICE_IEC_PRINTERLIST_H
#define DEVICE_IEC_PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        iecPrinter::printer_type type = iecPrinter::printer_type::PRINTER_INVALID;
        iecPrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, iecPrinter *ptr, iecPrinter::printer_type ptype, int pport);

    void set_ptr(int index, iecPrinter *ptr);
    void set_type(int index, iecPrinter::printer_type ptype);
    void set_port(int index, int pport);

    iecPrinter * get_ptr(int index);
    iecPrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // DEVICE_IEC_PRINTERLIST_H