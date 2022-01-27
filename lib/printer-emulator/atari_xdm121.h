#ifndef XDM121_H
#define XDM121_H

#ifdef BUILD_ATARI
#include "sio/printer.h"
#elif BUILD_CBM
#include "../device/iec/printer.h"
#elif BUILD_ADAM
#include "adamnet/printer.h"
#endif

#include "epson_80.h"

class xdm121 : public pdfPrinter
{
protected:
    // intl array copied from atari_1027.h - maybe better to make it a static const array of printer.h (the SIO device?)
    const uint8_t intlchar[27] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197};
    bool intlFlag = false;

    void not_implemented();
    void esc_not_implemented();

    struct epson_cmd_t
    {
        uint8_t cmd = 0;
        uint8_t N1 = 0;
        uint8_t N2 = 0;
        uint16_t N = 0;
        uint16_t ctr = 0;
    } epson_cmd;
    bool escMode;
    void reset_cmd();

    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void pdf_clear_modes() override{};
    void at_reset();
    virtual void post_new_file() override;

    double charPitch = 7.2;
    double wheelSize = 12;
    int back_spacing = 600;

    const int fnt_regular = 0;
    const int fnt_underline = 0x001;
    const int fnt_doublestrike = 0x002;
    uint16_t epson_font_mask = 0;

    uint8_t xdm_font_lookup(uint16_t code);
    void xdm_set_font(uint8_t F);
    void set_mode(uint16_t m);
    void clear_mode(uint16_t m);

public:
    const char *modelname()  override 
    {  
        #ifdef BUILD_ATARI
            return sioPrinter::printer_model_str[sioPrinter::PRINTER_ATARI_XDM121];
        #elif BUILD_CBM
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_ATARI_XDM121];
        #elif BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_XDM121];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    };
};

#endif