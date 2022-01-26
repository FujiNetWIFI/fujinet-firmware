#ifndef HTML_PRINTER_H
#define HTML_PRINTER_H

#ifdef BUILD_ATARI
#include "sio/printer.h"
#elif BUILD_CBM
#include "iec/printer.h"
#elif BUILD_ADAM
#include "adamnet/printer.h"
#endif

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
            #ifdef BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_HTML];
            #else
                #ifdef BUILD_ATARI
                    return sioPrinter::printer_model_str[sioPrinter::PRINTER_HTML];
                #else
                    return PRINTER_UNSUPPORTED;
                #endif
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
