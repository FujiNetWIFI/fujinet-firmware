#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        cx16Printer::printer_type type = cx16Printer::printer_type::PRINTER_INVALID;
        cx16Printer *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, cx16Printer *ptr, cx16Printer::printer_type ptype, int pport);

    void set_ptr(int index, cx16Printer *ptr);
    void set_type(int index, cx16Printer::printer_type ptype);
    void set_port(int index, int pport);

    cx16Printer * get_ptr(int index);
    cx16Printer::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H
