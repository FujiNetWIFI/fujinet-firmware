#ifndef _PRINTERLIST_H
#define _PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4
class printerlist
{
private:
    struct printerlist_entry
    {
        rs232Printer::printer_type type = rs232Printer::printer_type::PRINTER_INVALID;
        rs232Printer *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, rs232Printer *ptr, rs232Printer::printer_type ptype, int pport);

    void set_ptr(int index, rs232Printer *ptr);
    void set_type(int index, rs232Printer::printer_type ptype);
    void set_port(int index, int pport);

    rs232Printer * get_ptr(int index);
    rs232Printer::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif // _PRINTERLIST_H
