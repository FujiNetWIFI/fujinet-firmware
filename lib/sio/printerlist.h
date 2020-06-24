#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        sioPrinter::printer_type type = sioPrinter::printer_type::PRINTER_INVALID;
        sioPrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, sioPrinter *ptr, sioPrinter::printer_type ptype, int pport);

    void set_ptr(int index, sioPrinter *ptr);
    void set_type(int index, sioPrinter::printer_type ptype);
    void set_port(int index, int pport);

    sioPrinter * get_ptr(int index);
    sioPrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H
