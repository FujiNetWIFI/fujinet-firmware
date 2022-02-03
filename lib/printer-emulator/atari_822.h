#ifndef _ATARI822_H
#define _ATARI822_H

#include "printer.h"

#include "pdf_printer.h"

class atari822 : public pdfPrinter
{
protected:
    int gfxNumber = 0;
    virtual void pdf_clear_modes() override {};
    virtual void post_new_file() override;
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override; // need a custom one to handle sideways printing

public:
    const char *modelname()  override 
    {
        #ifdef BUILD_ATARI
            return sioPrinter::printer_model_str[sioPrinter::PRINTER_ATARI_822];
        #elif BUILD_CBM
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_ATARI_822];
        #elif BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_822];
        #elif NEW_TARGET
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_822];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    };

};

#endif // _ATARI822_H
