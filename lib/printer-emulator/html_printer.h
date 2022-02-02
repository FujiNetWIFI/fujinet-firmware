#ifndef HTML_PRINTER_H
#define HTML_PRINTER_H

#include "printer.h"

#include "printer_emulator.h"

class htmlPrinter : public printer_emu
{
private:
    bool inverse = false;

protected:
    virtual void post_new_file() override;
    virtual void pre_close_file() override;
    virtual bool process_buffer(uint8_t linelen, uint8_t aux1, uint8_t aux2);

public:
    htmlPrinter(paper_t ptype=HTML) { _paper_type = ptype; };

    const char *modelname() override  
    { 
        if (_paper_type == HTML)
        {
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_HTML];
            #elif BUILD_CBM
                return iecPrinter::printer_model_str[iecPrinter::PRINTER_HTML];
            #elif BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_HTML];
            #elif NEW_TARGET
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_HTML];
            #else
                return PRINTER_UNSUPPORTED;
            #endif
        }
        if (_paper_type == HTML_ATASCII)
        {
            #if  BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_HTML_ATASCII];
            #endif
        }
        return PRINTER_UNSUPPORTED;
  
    };
};

#endif
