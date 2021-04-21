#ifndef _ATARI820_H
#define _ATARI820_H

#include "pdf_printer.h"
#include "../bus/sio/printer.h"

class atari820 : public pdfPrinter
{
    // reverse the buffer in sioPrinter::sio_write() for sideways printing
    // the PDF standard doesn't really handle right-to-left
    // printing. The example in section 9.7 uses reverse strings.
protected:
    bool sideFlag = false;
    //sioPrinter *my_sioP; // added variable to point back to sioPrinter parent
    virtual void pdf_clear_modes() override {};
    virtual void post_new_file() override;
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override; // need a custom one to handle sideways printing

public:
    //atari820(sioPrinter *P) { my_sioP = P; }
    //void initPrinter();
    // void setDevice(sioPrinter *P) { my_sioP = P; };
    const char *modelname()  override { return "Atari 820"; };
};

#endif // _ATARI820_H
