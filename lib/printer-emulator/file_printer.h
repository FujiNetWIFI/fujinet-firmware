#ifndef FILE_PRINTER_H
#define FILE_PRINTER_H

#ifdef BUILD_ADAM
#include "adamnet/printer.h"
#endif
#ifdef BUILD_ATARI
#include "sio/printer.h"
#endif

#include "printer_emulator.h"

class filePrinter : public printer_emu
{
    virtual bool process_buffer(uint8_t linelen, uint8_t aux1, uint8_t aux2);
    virtual void post_new_file() {};
    virtual void pre_close_file() {};

public:
    filePrinter(paper_t ptype=TRIM) { _paper_type = ptype; };

    const char *modelname()  override 
    { 
        if (_paper_type == ASCII)
        {
            #ifdef BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_ASCII];
            #endif
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_FILE_ASCII];
            #endif
            return PRINTER_UNSUPPORTED;
        }
        else if (_paper_type == RAW)
        {
            #ifdef BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_RAW];
            #endif
            #ifdef  BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_FILE_RAW];
            #endif
            return PRINTER_UNSUPPORTED;
        }
        else
        {
            #ifdef BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_TRIM];
            #endif
            #ifdef  BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_FILE_TRIM];
            #endif
            return PRINTER_UNSUPPORTED;
        }

    };
};

#endif
