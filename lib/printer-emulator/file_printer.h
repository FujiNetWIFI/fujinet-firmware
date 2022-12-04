#ifndef FILE_PRINTER_H
#define FILE_PRINTER_H

#include "printer.h"

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
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_FILE_ASCII];
            #elif BUILD_CBM
                return iecPrinter::printer_model_str[iecPrinter::PRINTER_FILE_ASCII];
            #elif BUILD_APPLE
                return iwmPrinter::printer_model_str[iwmPrinter::PRINTER_FILE_ASCII];
            #elif BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_ASCII];
            #elif NEW_TARGET
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_ASCII];
            #else
                return PRINTER_UNSUPPORTED;
            #endif
        }
        else if (_paper_type == RAW)
        {
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_FILE_RAW];
            #elif BUILD_CBM
                return iecPrinter::printer_model_str[iecPrinter::PRINTER_FILE_RAW];
            #elif BUILD_APPLE
                return iwmPrinter::printer_model_str[iwmPrinter::PRINTER_FILE_RAW];
            #elif BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_RAW];
            #elif NEW_TARGET
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_RAW];
            #else
                return PRINTER_UNSUPPORTED;
            #endif
        }
        else
        {
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_FILE_TRIM];
            #elif BUILD_CBM
                return iecPrinter::printer_model_str[iecPrinter::PRINTER_FILE_TRIM];
            #elif BUILD_APPLE
                return iwmPrinter::printer_model_str[iwmPrinter::PRINTER_FILE_TRIM];
            #elif BUILD_ADAM
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_TRIM];
            #elif NEW_TARGET
                return adamPrinter::printer_model_str[adamPrinter::PRINTER_FILE_TRIM];
            #else
                return PRINTER_UNSUPPORTED;
            #endif
        }

    };
};

#endif
