#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        adamPrinter::printer_type type = adamPrinter::printer_type::PRINTER_INVALID;
        adamPrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, adamPrinter *ptr, adamPrinter::printer_type ptype, int pport);

    void set_ptr(int index, adamPrinter *ptr);
    void set_type(int index, adamPrinter::printer_type ptype);
    void set_port(int index, int pport);

    adamPrinter * get_ptr(int index);
    adamPrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H