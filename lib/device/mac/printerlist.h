#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        macPrinter::printer_type type = macPrinter::printer_type::PRINTER_INVALID;
        macPrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, macPrinter *ptr, macPrinter::printer_type ptype, int pport);

    void set_ptr(int index, macPrinter *ptr);
    void set_type(int index, macPrinter::printer_type ptype);
    void set_port(int index, int pport);

    macPrinter * get_ptr(int index);
    macPrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H