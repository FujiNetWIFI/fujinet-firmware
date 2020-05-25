#ifndef _ATARI822_H
#define _ATARI822_H

#include "pdf_printer.h"
#include "../sio/printer.h"

class atari822 : public pdfPrinter
{
protected:
    //sioPrinter *my_sioP;

    void pdf_handle_char(byte c, byte aux1, byte aux2); // need a custom one to handle sideways printing

    int gfxNumber = 0;

public:
    //atari822(sioPrinter *P) { my_sioP = P; }
    virtual void initPrinter();
    const char *modelname() { return "Atari 822"; };

    //void setDevice(sioPrinter *P) { my_sioP = P; };
};

#endif // _ATARI822_H
