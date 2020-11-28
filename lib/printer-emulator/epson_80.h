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
    const uint16_t fnt_regular = 0;
    const uint16_t fnt_pica = 0x000;
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

    const uint16_t font_tab[153] =
    {
        fnt_pica | fnt_regular, // FXMatrix105MonoPicaRegular
        fnt_compressed | fnt_doublestrike | fnt_italic, // FXMatrix105MonoComprDblItalic
        fnt_compressed | fnt_doublestrike | fnt_regular, // FXMatrix105MonoComprDblRegular
        fnt_compressed | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105MonoComprDblSubItalic
        fnt_compressed | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105MonoComprDblSubRegular
        fnt_compressed | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105MonoComprDblSupItalic
        fnt_compressed | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105MonoComprDblSupRegular
        fnt_compressed | fnt_doublestrike | fnt_underline | fnt_italic, // FXMatrix105MonoComprDblULItalic
        fnt_compressed | fnt_doublestrike | fnt_underline | fnt_regular, // FXMatrix105MonoComprDblULRegular
        fnt_compressed | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105MonoComprDblULSubItalic
        fnt_compressed | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105MonoComprDblULSubRegular
        fnt_pica | fnt_doublestrike | fnt_regular, // FXMatrix105MonoPicaDblRegular
        fnt_compressed | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105MonoComprDblULSupItalic
        fnt_compressed | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105MonoComprDblULSupRegular
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_italic, // FXMatrix105MonoComprExpDblItalic
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_regular, // FXMatrix105MonoComprExpDblRegular
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105MonoComprExpDblSubItalic
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105MonoComprExpDblSubRegular
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105MonoComprExpDblSupItalic
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105MonoComprExpDblSupRegular
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_italic, // FXMatrix105MonoComprExpDblULItalic
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_regular, // FXMatrix105MonoComprExpDblULRegular
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105MonoComprExpDblULSubItalic
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105MonoComprExpDblULSubRegular
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105MonoComprExpDblULSupItalic
        fnt_compressed | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105MonoComprExpDblULSupRegular
        fnt_compressed | fnt_expanded | fnt_italic, // FXMatrix105MonoComprExpItalic
        fnt_compressed | fnt_expanded | fnt_regular, // FXMatrix105MonoComprExpRegular
        fnt_compressed | fnt_expanded | fnt_underline | fnt_italic, // FXMatrix105MonoComprExpULItalic
        fnt_compressed | fnt_expanded | fnt_underline | fnt_regular, // FXMatrix105MonoComprExpULRegular
        fnt_compressed | fnt_italic, // FXMatrix105MonoComprItalic
        fnt_compressed | fnt_regular, // FXMatrix105MonoComprRegular
        fnt_compressed | fnt_underline | fnt_italic, // FXMatrix105MonoComprULItalic
        fnt_compressed | fnt_underline | fnt_regular, // FXMatrix105MonoComprULRegular
        fnt_elite | fnt_doublestrike | fnt_italic, // FXMatrix105MonoEliteDblItalic
        fnt_elite | fnt_doublestrike | fnt_regular, // FXMatrix105MonoEliteDblRegular
        fnt_elite | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105MonoEliteDblSubItalic
        fnt_elite | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105MonoEliteDblSubRegular
        fnt_elite | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105MonoEliteDblSupItalic
        fnt_elite | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105MonoEliteDblSupRegular
        fnt_elite | fnt_doublestrike | fnt_underline | fnt_italic, // FXMatrix105MonoEliteDblULItalic
        fnt_elite | fnt_doublestrike | fnt_underline | fnt_regular, // FXMatrix105MonoEliteDblULRegular
        fnt_elite | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105MonoEliteDblULSubItalic
        fnt_elite | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105MonoEliteDblULSubRegular
        fnt_elite | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105MonoEliteDblULSupItalic
        fnt_elite | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105MonoEliteDblULSupRegular
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_italic, // FXMatrix105MonoEliteExpDblItalic
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_regular, // FXMatrix105MonoEliteExpDblRegular
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105MonoEliteExpDblSubItalic
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105MonoEliteExpDblSubRegular
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105MonoEliteExpDblSupItalic
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105MonoEliteExpDblSupRegular
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_italic, // FXMatrix105MonoEliteExpDblULItalic
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_regular, // FXMatrix105MonoEliteExpDblULRegular
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105MonoEliteExpDblULSubItalic
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105MonoEliteExpDblULSubRegular
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105MonoEliteExpDblULSupItalic
        fnt_elite | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105MonoEliteExpDblULSupRegular
        fnt_elite | fnt_expanded | fnt_italic, // FXMatrix105MonoEliteExpItalic
        fnt_elite | fnt_expanded | fnt_regular, // FXMatrix105MonoEliteExpRegular
        fnt_elite | fnt_expanded | fnt_underline | fnt_italic, // FXMatrix105MonoEliteExpULItalic
        fnt_elite | fnt_expanded | fnt_underline | fnt_regular, // FXMatrix105MonoEliteExpULRegular
        fnt_elite | fnt_italic, // FXMatrix105MonoEliteItalic
        fnt_elite | fnt_regular, // FXMatrix105MonoEliteRegular
        fnt_elite | fnt_underline | fnt_italic, // FXMatrix105MonoEliteULItalic
        fnt_elite | fnt_underline | fnt_regular, // FXMatrix105MonoEliteULRegular
        fnt_pica | fnt_emphasized, // FXMatrix105MonoPicaBold
        fnt_pica | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaBoldItalic
        fnt_pica | fnt_doublestrike | fnt_emphasized, // FXMatrix105MonoPicaDblBold
        fnt_pica | fnt_doublestrike | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaDblBoldItalic
        fnt_pica | fnt_doublestrike | fnt_italic, // FXMatrix105MonoPicaDblItalic
        fnt_pica | fnt_doublestrike | fnt_subscript | fnt_emphasized, // FXMatrix105MonoPicaDblSubBold
        fnt_pica | fnt_doublestrike | fnt_subscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaDblSubBoldItalic
        fnt_pica | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105MonoPicaDblSubItalic
        fnt_pica | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105MonoPicaDblSubRegular
        fnt_pica | fnt_doublestrike | fnt_superscript | fnt_emphasized, // FXMatrix105MonoPicaDblSupBold
        fnt_pica | fnt_doublestrike | fnt_superscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaDblSupBoldItalic
        fnt_pica | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105MonoPicaDblSupItalic
        fnt_pica | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105MonoPicaDblSupRegular
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_emphasized, // FXMatrix105MonoPicaDblULBold
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaDblULBoldItalic
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_regular, // FXMatrix105MonoPicaDblULRegular
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_italic, // FXMatrix105MonoPicaDblULItalic
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_emphasized, // FXMatrix105MonoPicaDblULSubBold
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaDblULSubBoldItalic
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105MonoPicaDblULSubItalic
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105MonoPicaDblULSubRegular
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_emphasized, // FXMatrix105MonoPicaDblULSupBold
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaDblULSupBoldItalic
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105MonoPicaDblULSupItalic
        fnt_pica | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105MonoPicaDblULSupRegular
        fnt_pica | fnt_expanded | fnt_emphasized, // FXMatrix105MonoPicaExpBold
        fnt_pica | fnt_expanded | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpBoldItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_emphasized, // FXMatrix105MonoPicaExpDblBold
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpDblBoldItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_italic, // FXMatrix105MonoPicaExpDblItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_regular, // FXMatrix105MonoPicaExpDblRegular
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_emphasized, // FXMatrix105MonoPicaExpDblSubBold
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpDblSubBoldItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105MonoPicaExpDblSubItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105MonoPicaExpDblSubRegular
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_emphasized, // FXMatrix105MonoPicaExpDblSupBold
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpDblSupBoldItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105MonoPicaExpDblSupItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105MonoPicaExpDblSupRegular
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_emphasized, // FXMatrix105MonoPicaExpDblULBold
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpDblULBoldItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_italic, // FXMatrix105MonoPicaExpDblULItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_regular, // FXMatrix105MonoPicaExpDblULRegular
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_emphasized, // FXMatrix105MonoPicaExpDblULSubBold
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpDblULSubBoldItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105MonoPicaExpDblULSubItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105MonoPicaExpDblULSubRegular
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_emphasized, // FXMatrix105MonoPicaExpDblULSupBold
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpDblULSupBoldItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105MonoPicaExpDblULSupItalic
        fnt_pica | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105MonoPicaExpDblULSupRegular
        fnt_pica | fnt_expanded | fnt_italic, // FXMatrix105MonoPicaExpItalic
        fnt_pica | fnt_expanded | fnt_regular, // FXMatrix105MonoPicaExpRegular
        fnt_pica | fnt_expanded | fnt_underline | fnt_emphasized, // FXMatrix105MonoPicaExpULBold
        fnt_pica | fnt_expanded | fnt_underline | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaExpULBoldItalic
        fnt_pica | fnt_expanded | fnt_underline | fnt_italic, // FXMatrix105MonoPicaExpULItalic
        fnt_pica | fnt_expanded | fnt_underline | fnt_regular, // FXMatrix105MonoPicaExpULRegular
        fnt_pica | fnt_italic, // FXMatrix105MonoPicaItalic
        fnt_pica | fnt_underline | fnt_emphasized, // FXMatrix105MonoPicaULBold
        fnt_pica | fnt_underline | fnt_emphasized | fnt_italic, // FXMatrix105MonoPicaULBoldItalic
        fnt_pica | fnt_underline | fnt_italic, // FXMatrix105MonoPicaULItalic
        fnt_pica | fnt_underline | fnt_regular, // FXMatrix105MonoPicaULRegular
        fnt_proportional | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105PropPicaDblSubItalic
        fnt_proportional | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105PropPicaDblSubRegular
        fnt_proportional | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105PropPicaDblSupItalic
        fnt_proportional | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105PropPicaDblSupRegular
        fnt_proportional | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105PropPicaDblULSubItalic
        fnt_proportional | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105PropPicaDblULSubRegular
        fnt_proportional | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105PropPicaDblULSupItalic
        fnt_proportional | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105PropPicaDblULSupRegular
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_italic, // FXMatrix105PropPicaExpDblSubItalic
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_subscript | fnt_regular, // FXMatrix105PropPicaExpDblSubRegular
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_italic, // FXMatrix105PropPicaExpDblSupItalic
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_superscript | fnt_regular, // FXMatrix105PropPicaExpDblSupRegular
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_italic, // FXMatrix105PropPicaExpDblULSubItalic
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_subscript | fnt_regular, // FXMatrix105PropPicaExpDblULSubRegular
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_italic, // FXMatrix105PropPicaExpDblULSupItalic
        fnt_proportional | fnt_expanded | fnt_doublestrike | fnt_underline | fnt_superscript | fnt_regular, // FXMatrix105PropPicaExpDblULSupRegular
        fnt_proportional | fnt_expanded | fnt_italic, // FXMatrix105PropPicaExpItalic
        fnt_proportional | fnt_expanded | fnt_regular, // FXMatrix105PropPicaExpRegular
        fnt_proportional | fnt_expanded | fnt_underline | fnt_italic, // FXMatrix105PropPicaExpULItalic
        fnt_proportional | fnt_expanded | fnt_underline | fnt_regular, // FXMatrix105PropPicaExpULRegular
        fnt_proportional | fnt_regular, // FXMatrix105PropPicaRegular
        fnt_proportional | fnt_italic, // FXMatrix105PropPicaItalic
        fnt_proportional | fnt_emphasized, // FXMatrix105PropPicaRegular,Bold
        fnt_proportional | fnt_emphasized | fnt_italic // FXMatrix105PropPicaItalic,Bold
    };

};

#endif