#ifndef ATARI_1029_H
#define ATARI_1029_H

#ifdef BUILD_ADAM
#include "adamnet/printer.h"
#endif
#ifdef BUILD_ATARI
#include "sio/printer.h"
#endif

#include "pdf_printer.h"


class atari1029 : public pdfPrinter
{
protected:
    const uint8_t intlchar[32] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 0, 0xa0 + 28, 0xa0 + 29, 0xa0 + 30, 0xa0 + 31};

    struct epson_cmd_t
    {
        uint8_t cmd = 0;
        uint8_t N1 = 0;
        uint8_t N2 = 0;
        uint16_t N = 0;
        uint16_t ctr = 0;
    } epson_cmd;
    bool escMode = false;

    const uint16_t fnt_underline = 0x001;
    const uint16_t fnt_expanded = 0x002;
    bool intlFlag = false;

    uint16_t epson_font_mask = 0; // need to set to normal TODO

    void print_8bit_gfx(uint8_t c);
    void not_implemented();
    void esc_not_implemented();
    void set_mode(uint16_t m);
    void clear_mode(uint16_t m);
    void reset_cmd();
    uint8_t epson_font_lookup(uint16_t code);
    double epson_font_width(uint16_t code);
    void epson_set_font(uint8_t F, double w);

    virtual void pdf_clear_modes() override;
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

public:
    const char *modelname()  override 
    { 
        #ifdef BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_1029];
        #else
            #ifdef BUILD_ATARI
                return sioPrinter::printer_model_str[sioPrinter::PRINTER_ATARI_1029];
            #else
                return PRINTER_UNSUPPORTED;
            #endif
        #endif
    };
};

#endif