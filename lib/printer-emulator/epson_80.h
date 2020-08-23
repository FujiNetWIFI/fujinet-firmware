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

    const uint16_t fnt_underline = 0x001;
    const uint16_t fnt_italic = 0x002;
    const uint16_t fnt_expanded = 0x004;
    const uint16_t fnt_compressed = 0x008;
    const uint16_t fnt_emphasized = 0x010;
    const uint16_t fnt_doublestrike = 0x020;
    const uint16_t fnt_superscript = 0x040;
    const uint16_t fnt_subscript = 0x080;
    const uint16_t fnt_SOwide = 0x100;
    const uint16_t fnt_elite = 0x200;
    const uint16_t fnt_proportional = 0x400;

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
};

#endif