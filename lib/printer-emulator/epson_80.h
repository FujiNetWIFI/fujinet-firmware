#ifndef EPSON_80_H
#define EPSON_80_H

#include "pdf_printer.h"


class epson80 : public pdfPrinter
{
protected:
    struct epson_cmd_t
    {
        uint8_t cmd = 0;
        uint8_t N1 = 0;
        uint8_t N2 = 0;
        uint16_t N= 0;
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
    void at_reset();

   virtual void pdf_clear_modes() override {};
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;
public:
    const char *modelname() { return "Epson 80"; };

private:
    // had to use "int" here because "uint16_t" gave a compile error
    const int fnt_regular = 0;
    const int fnt_pica = 0x000;
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

    const int font_tab[32] =
        {
            fnt_regular,                                                      // MonoPicaRegular
            fnt_doublestrike | fnt_regular,                                   // MonoPicaDblRegular
            fnt_italic,                                                       // MonoPicaItalic
            fnt_doublestrike | fnt_italic,                                    // MonoPicaDblItalic
            fnt_emphasized,                                                   // MonoPicaBold
            fnt_underline | fnt_regular,                                      // MonoPicaULRegular
            fnt_doublestrike | fnt_underline | fnt_regular,                   // MonoPicaDblULRegular
            fnt_underline | fnt_italic,                                       // MonoPicaULItalic
            fnt_doublestrike | fnt_underline | fnt_italic,                    // MonoPicaDblULItalic
            fnt_expanded | fnt_regular,                                       // MonoPicaExpRegular
            fnt_expanded | fnt_doublestrike | fnt_regular,                    // MonoPicaExpDblRegular
            fnt_expanded | fnt_italic,                                        // MonoPicaExpItalic
            fnt_expanded | fnt_doublestrike | fnt_italic,                     // MonoPicaExpDblItalic
            fnt_expanded | fnt_underline | fnt_regular,                       // MonoPicaExpULRegular
            fnt_expanded | fnt_doublestrike | fnt_underline | fnt_regular,    // MonoPicaExpDblULRegular
            fnt_expanded | fnt_underline | fnt_italic,                        // MonoPicaExpULItalic
            fnt_expanded | fnt_doublestrike | fnt_underline | fnt_italic,     // MonoPicaExpDblULItalic
            fnt_expanded | fnt_underline | fnt_emphasized,                    // MonoPicaExpULBold
            fnt_expanded | fnt_doublestrike | fnt_emphasized,                 // MonoPicaExpDblBold
            fnt_doublestrike | fnt_emphasized,                                // MonoPicaDblBold
            fnt_emphasized | fnt_italic,                                      // MonoPicaBoldItalic
            fnt_doublestrike | fnt_italic,                                    // MonoPicaDblBoldItalic
            fnt_underline | fnt_emphasized,                                   // MonoPicaULBold
            fnt_doublestrike | fnt_underline | fnt_emphasized,                // MonoPicaDblULBold
            fnt_underline | fnt_italic,                                       // MonoPicaULBoldItalic
            fnt_doublestrike | fnt_underline | fnt_italic,                    // MonoPicaDblULBoldItalic
            fnt_expanded | fnt_emphasized,                                    // MonoPicaExpBold
            fnt_expanded | fnt_italic,                                        // MonoPicaExpBoldItalic
            fnt_expanded | fnt_doublestrike | fnt_italic,                     // MonoPicaExpDblBoldItalic
            fnt_expanded | fnt_doublestrike | fnt_underline | fnt_emphasized, // MonoPicaExpDblULBold
            fnt_expanded | fnt_underline | fnt_italic,                        // MonoPicaExpULBoldItalic
            fnt_expanded | fnt_doublestrike | fnt_underline | fnt_italic      // MonoPicaExpDblULBoldItalic
        };
};

#endif