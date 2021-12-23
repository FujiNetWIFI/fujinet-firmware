#ifndef ATARI_1020_H
#define ATARI_1020_H


#include "svg_plotter.h"

// to do: double check this against 1020 and 1027 test output and manuals
const uint8_t intlchar[28] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 
                              243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 27};

class atari1020 : public svgPlotter
{
    const char *modelname()  override 
    {
        #ifdef BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_1020];
        #else
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_ATARI_1020];
            #else
                return PRINTER_UNSUPPORTED;
            #endif
        #endif

    };
};

#endif