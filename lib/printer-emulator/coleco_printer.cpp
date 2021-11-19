#include "coleco_printer.h"
#include "../../include/debug.h"

void colecoprinter::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
{
    if (c == 0x0a)
    {
    }
    else if (c == 0x0b) // VT - Half line feed
    {
        pdf_dY -= lineHeight / 2.;
        pdf_set_rise();
    }
    else if (c == 0x0c)
    {
        pdf_end_page();
    }
    else if (c == 0x0d)
    {
        pdf_end_line();
    }
    else if (c > 31 && c < 128)
    {
        if (c == 123 || c == 125 || c == 127)
            c = ' ';
        if (c == '\\' || c == '(' || c == ')')
            fputc('\\', _file);
        fputc(c, _file);

        pdf_X += charWidth; // update x position
    }
}

void colecoprinter::post_new_file()
{
    shortname = "a1027";

    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 66.0;
    bottomMargin = 0;
    printWidth = 480.0; // 6 2/3 inches
    lineHeight = 12.0;
    charWidth = 6.0; // 12cpi
    fontNumber = 1;
    fontSize = 10;

    pdf_header();
}
