#ifdef BUILD_ATARI

#include "printerlist.h"

// Global object to hold our printers
printerlist fnPrinters;

void printerlist::set_entry(int index, sioPrinter *ptr, sioPrinter::printer_type ptype, int pport)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return;

    _printers[index].pPrinter = ptr;
    _printers[index].type = ptype;
    _printers[index].port = pport;
}
void printerlist::set_ptr(int index, sioPrinter *ptr)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return;
    _printers[index].pPrinter = ptr;
}
void printerlist::set_type(int index, sioPrinter::printer_type ptype)
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
sioPrinter * printerlist::get_ptr(int index)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return nullptr;
    return _printers[index].pPrinter;
}
sioPrinter::printer_type printerlist::get_type(int index)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return sioPrinter::printer_type::PRINTER_INVALID;
    return _printers[index].type;
}
int printerlist::get_port(int index)
{
    if(index < 0 || index >= PRINTERLIST_SIZE)
        return -1;
    return _printers[index].port;
}

#endif /* BUILD_ATARI */