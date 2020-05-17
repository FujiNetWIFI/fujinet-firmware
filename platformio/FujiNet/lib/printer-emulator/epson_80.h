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
        byte ctr;
    } epson_cmd = {0, 0, 0, 0};
    bool escMode = false;

    const uint16_t fnt_underline = 0x01;
    const uint16_t fnt_expanded = 0x02; 
    const uint16_t fnt_compressed = 0x04;
    const uint16_t fnt_italic = 0x08;
    const uint16_t fnt_emphasized = 0x10;
    const uint16_t fnt_doublestrike = 0x20;
    const uint16_t fnt_superscript = 0x40; 
    const uint16_t fnt_subscript = 0x80;
    const uint16_t fnt_SOwide = 0x100;

    uint16_t epson_font_mask = 0; // need to set to normal TODO

    void not_implemented();
    void esc_not_implemented();
    void set_mode(uint16_t m);
    void clear_mode(uint16_t m);
    void reset_cmd();
    void pdf_handle_char(byte c);

public:
    void initPrinter(FS *filesystem);
    const char *modelname() { return "Epson 80"; };
};

#endif