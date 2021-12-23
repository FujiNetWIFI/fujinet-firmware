#ifndef XMM801_H
#define XMM801_H

#ifdef BUILD_ADAM
#include "adamnet/printer.h"
#endif
#ifdef BUILD_ATARI
#include "sio/printer.h"
#endif

#include "epson_80.h"

class xmm801 : public epson80
{
protected:
    // intl array copied from atari_1025.h - maybe better to make it a static const array of printer.h (the SIO device?)
    const uint8_t intlchar[32] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 0, 0xa0 + 28, 0xa0 + 29, 0xa0 + 30, 0xa0 + 31};
    bool intlFlag = false;
    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override; 

public:
    const char *modelname()  override 
    { 
        #ifdef BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_XMM801];
        #else
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_ATARI_XMM801];
            #else
                return PRINTER_UNSUPPORTED;
            #endif
        #endif
    };
};

#endif