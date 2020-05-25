#ifndef _ATARI822_H
#define _ATARI822_H

#include "pdf_printer.h"
#include "../sio/printer.h"

class atari822 : public pdfPrinter
{
protected:
    int gfxNumber = 0;

    virtual void post_new_file() override;
    void pdf_handle_char(byte c, byte aux1, byte aux2) override; // need a custom one to handle sideways printing

public:
    const char *modelname() { return "Atari 822"; };
};

#endif // _ATARI822_H
