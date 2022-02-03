#ifndef EPSON_TPS_H
#define EPSON_TPS_H

#include "printer.h"

#include "epson_80.h"

class epsonTPS : public epson80
{
protected:
    // virtual void pdf_clear_modes() override{};
    virtual void post_new_file() override
    {
        epson80::post_new_file();
        pdf_dY = lineHeight;
    }; // go up one line for The Print Shop

public:
    const char *modelname()  override 
    { 
        #ifdef BUILD_ATARI
            return sioPrinter::printer_model_str[sioPrinter::PRINTER_EPSON_PRINTSHOP];
        #elif BUILD_CBM
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_EPSON_PRINTSHOP];
        #elif BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_EPSON_PRINTSHOP];
        #elif NEW_TARGET
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_EPSON_PRINTSHOP];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    }
};

#endif