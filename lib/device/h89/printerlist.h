#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        H89Printer::printer_type type = H89Printer::printer_type::PRINTER_INVALID;
        H89Printer *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, H89Printer *ptr, H89Printer::printer_type ptype, int pport);

    void set_ptr(int index, H89Printer *ptr);
    void set_type(int index, H89Printer::printer_type ptype);
    void set_port(int index, int pport);

    H89Printer * get_ptr(int index);
    H89Printer::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H