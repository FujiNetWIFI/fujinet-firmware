#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        applePrinter::printer_type type = applePrinter::printer_type::PRINTER_INVALID;
        applePrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, applePrinter *ptr, applePrinter::printer_type ptype, int pport);

    void set_ptr(int index, applePrinter *ptr);
    void set_type(int index, applePrinter::printer_type ptype);
    void set_port(int index, int pport);

    applePrinter * get_ptr(int index);
    applePrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H