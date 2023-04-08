#ifdef BUILD_CDC

#include "printerlist.h"

// Global object to hold our printers
printerlist fnPrinters;

void printerlist::set_entry(int index, cdcPrinter *ptr, cdcPrinter::printer_type ptype, int pport)
{
    if (ptr == NULL)
        return;

    if(index < 0 || index >= PRINTERLIST_SIZE)
        return;

    _printers[index].pPrinter = ptr;
    _printers[index].type = ptype;
    _printers[index].port = pport;
}
void printerlist::set_ptr(int index, cdcPrinter *ptr)
{
    if (ptr == NULL)
        return;
        
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return;
    _printers[index].pPrinter = ptr;
}
void printerlist::set_type(int index, cdcPrinter::printer_type ptype)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return;
    _printers[index].type = ptype;
}
void printerlist::set_port(int index, int pport)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return;
    _printers[index].port = pport;
}
cdcPrinter * printerlist::get_ptr(int index)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return nullptr;
    return _printers[index].pPrinter;
}
cdcPrinter::printer_type printerlist::get_type(int index)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return cdcPrinter::printer_type::PRINTER_INVALID;
    return _printers[index].type;
}
int printerlist::get_port(int index)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return -1;
    return _printers[index].port;
}

#endif /* BUILD_CDC */
