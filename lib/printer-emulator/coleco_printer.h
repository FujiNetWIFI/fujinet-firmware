#ifndef COLECO_PRINTER_H
#define COLECO_PRINTER_H

#include "printer.h"

#include "pdf_printer.h"

class colecoprinter : public pdfPrinter
{
protected:

   virtual void pdf_clear_modes() override {};
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2);
    virtual void post_new_file() override;
public:
    const char *modelname()  override 
    { 
        #if BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_COLECO_ADAM];
        #elif NEW_TARGET
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_COLECO_ADAM];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    };
};

#endif
