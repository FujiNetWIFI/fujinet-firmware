#ifndef PRINTERLIST_H
#define PRINTERLIST_H

#include "printer.h"

#define PRINTERLIST_SIZE 4

class printerlist
{
private:
    struct printerlist_entry
    {
        cdcPrinter::printer_type type = cdcPrinter::printer_type::PRINTER_INVALID;
        cdcPrinter *pPrinter = nullptr;
        int port = 0;
    };
    printerlist_entry _printers[PRINTERLIST_SIZE];

public:
    void set_entry(int index, cdcPrinter *ptr, cdcPrinter::printer_type ptype, int pport);

    void set_ptr(int index, cdcPrinter *ptr);
    void set_type(int index, cdcPrinter::printer_type ptype);
    void set_port(int index, int pport);

    cdcPrinter * get_ptr(int index);
    cdcPrinter::printer_type get_type(int index);
    int get_port(int index);
};

extern printerlist fnPrinters;

#endif /* PRINTERLIST_H */