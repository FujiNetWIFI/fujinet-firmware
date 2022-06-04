#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        lynxPrinter::printer_type type = lynxPrinter::printer_type::PRINTER_INVALID;
        lynxPrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, lynxPrinter *ptr, lynxPrinter::printer_type ptype, int pport);

    void set_ptr(int index, lynxPrinter *ptr);
    void set_type(int index, lynxPrinter::printer_type ptype);
    void set_port(int index, int pport);

    lynxPrinter * get_ptr(int index);
    lynxPrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H