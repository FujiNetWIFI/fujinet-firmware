#ifndef _ATARI822_H
#define _ATARI822_H

#include "pdf_printer.h"
#include "../bus/sio/printer.h"

class atari822 : public pdfPrinter
{
protected:
    int gfxNumber = 0;
   virtual void pdf_clear_modes() override {};
    virtual void post_new_file() override;
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override; // need a custom one to handle sideways printing

public:
    const char *modelname() { return "Atari 822"; };
};

#endif // _ATARI822_H
