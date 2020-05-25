#ifndef EPSON_80_H
#define EPSON_80_H
#include <Arduino.h>

#include "pdf_printer.h"

class epson80 : public pdfPrinter
{
protected:
    struct epson_cmd_t
    {
        byte cmd;
        byte N1;
        byte N2;
        uint16_t N;
        uint16_t ctr;
    } epson_cmd = {0, 0, 0, 0};
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

    void print_8bit_gfx(byte c);
    void not_implemented();
    void esc_not_implemented();
    void set_mode(uint16_t m);
    void clear_mode(uint16_t m);
    void reset_cmd();
    byte epson_font_lookup(uint16_t code);
    float epson_font_width(uint16_t code);
    void epson_set_font(byte F, float w);
    void at_reset();

    void pdf_handle_char(byte c, byte aux1, byte aux2) override;
    virtual void post_new_file() override;
public:
    const char *modelname() { return "Epson 80"; };
};

#endif