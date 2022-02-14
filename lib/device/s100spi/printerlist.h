#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        s100spiPrinter::printer_type type = s100spiPrinter::printer_type::PRINTER_INVALID;
        s100spiPrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, s100spiPrinter *ptr, s100spiPrinter::printer_type ptype, int pport);

    void set_ptr(int index, s100spiPrinter *ptr);
    void set_type(int index, s100spiPrinter::printer_type ptype);
    void set_port(int index, int pport);

    s100spiPrinter * get_ptr(int index);
    s100spiPrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H