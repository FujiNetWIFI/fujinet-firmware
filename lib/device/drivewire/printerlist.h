#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        drivewirePrinter::printer_type type = drivewirePrinter::printer_type::PRINTER_INVALID;
        drivewirePrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, drivewirePrinter *ptr, drivewirePrinter::printer_type ptype, int pport);

    void set_ptr(int index, drivewirePrinter *ptr);
    void set_type(int index, drivewirePrinter::printer_type ptype);
    void set_port(int index, int pport);

    drivewirePrinter * get_ptr(int index);
    drivewirePrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H
