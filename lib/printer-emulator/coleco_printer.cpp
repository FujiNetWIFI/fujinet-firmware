#include "coleco_printer.h"
#include "../../include/debug.h"

void colecoprinter::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
{
    switch (c)
    {
    case 8:
        break;
    case 9:
        break;
    case 10:
        pdf_dY -= lineHeight; // set pdf_dY and rise to one line
        pdf_set_rise();
        break;
    case 11:
        pdf_dY -= 6.0; // set pdf_dY and rise to one line
        pdf_set_rise();
        break;
    case 12:
        pdf_end_page();
        break;
    case 13:
        pdf_dY += lineHeight; // set pdf_dY and rise back one line
        pdf_set_rise();
        pdf_end_line();
        break;
    default:
        if (c > 31 && c < 128)
        {
            if (c == 123 || c == 125 || c == 127)
                c = ' ';
            if (c == '\\' || c == '(' || c == ')')
                fputc('\\', _file);
            fputc(c, _file);

            pdf_X += charWidth; // update x position
        }
    }
}

void colecoprinter::post_new_file()
{
    shortname = "a1027";

    translate850 = false;
    _eol = 0;

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
