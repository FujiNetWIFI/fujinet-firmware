#ifndef _COMMODOREMPS803_H
#define _COMMODOREMPS803_H

#include "printer.h"

#include "pdf_printer.h"

class commodoremps803 : public pdfPrinter
{
protected:
    bool sideFlag = false;
    virtual void pdf_clear_modes() override {};
    virtual void post_new_file() override;
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override; // need a custom one to handle sideways printing

public:
    const char *modelname()  override 
    { 
        #ifdef BUILD_IEC
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_COMMODORE_MPS803];
        #elif BUILD_ATARI
            return sioPrinter::printer_model_str[sioPrinter::PRINTER_ATARI_820];
        #elif BUILD_CBM
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_ATARI_820];
        #elif BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_820];
        #elif NEW_TARGET
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_820];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    };
};

#endif // _COMMODOREMPS803_H