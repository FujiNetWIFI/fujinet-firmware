#ifndef EPSON_80_H
#define EPSON_80_H

#ifdef BUILD_ATARI
#include "sio/printer.h"
#elif BUILD_CBM
#include "../device/iec/printer.h"
#elif BUILD_ADAM
#include "adamnet/printer.h"
#endif

#include "pdf_printer.h"

#define NUMFONTS 15

class epson80 : public pdfPrinter
{
protected:
    struct epson_cmd_t
    {
        uint8_t cmd = 0;
        uint8_t N1 = 0;
        uint8_t N2 = 0;
        uint16_t N = 0;
        uint16_t ctr = 0;
    } epson_cmd;
    bool escMode = false;

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
    void at_reset();
    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

    // had to use "int" here because "uint16_t" gave a compile error
    const int fnt_regular = 0;
    const int fnt_underline = 0x001;
    const int fnt_italic = 0x002;
    const int fnt_expanded = 0x004;
    const int fnt_compressed = 0x008;
    const int fnt_emphasized = 0x010;
    const int fnt_doublestrike = 0x020;
    const int fnt_superscript = 0x040;
    const int fnt_subscript = 0x080;
    const int fnt_SOwide = 0x100;
    const int fnt_elite = 0x200;
    const int fnt_proportional = 0x400;

    const int font_tab[NUMFONTS - 1] =
        {
            fnt_regular,                                   // '/FXMatrix105MonoPicaRegular'
            fnt_doublestrike,                              // '/FXMatrix105MonoPicaDblRegular'
            fnt_italic,                                    // '/FXMatrix105MonoPicaItalic'
            fnt_doublestrike | fnt_italic,                 // '/FXMatrix105MonoPicaDblItalic'
            fnt_underline,                                 // '/FXMatrix105MonoPicaULRegular'
            fnt_doublestrike | fnt_underline,              // '/FXMatrix105MonoPicaDblULRegular'
            fnt_underline | fnt_italic,                    // '/FXMatrix105MonoPicaULItalic'
            fnt_doublestrike | fnt_underline | fnt_italic, // '/FXMatrix105MonoPicaDblULItalic'
            fnt_expanded,                                  // '/FXMatrix105MonoPicaExpRegular'
            fnt_expanded | fnt_underline,                  // '/FXMatrix105MonoPicaExpULRegular'
            fnt_compressed,                                // '/FXMatrix105MonoComprRegular'
            fnt_compressed | fnt_expanded,                 // '/FXMatrix105MonoComprExpRegular'
            fnt_elite,                                     // '/FXMatrix105MonoEliteRegular'
            fnt_elite | fnt_expanded                       // '/FXMatrix105MonoEliteExpRegular'
        };

public:
    const char *modelname()  override 
    { 
        #ifdef BUILD_ATARI
            return sioPrinter::printer_model_str[sioPrinter::PRINTER_EPSON];
        #elif BUILD_CBM
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_EPSON];
        #elif BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_EPSON];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    };

};

#endif