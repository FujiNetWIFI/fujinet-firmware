#include <cstring>
#include "coleco_printer.h"

void colecoprinter::line_output()
{
    for (int i=0;i<80;i++)
    {
        char c = bdb.get()[i];

        if (c > 31 && c < 128)
        {
            if (c == '\\' || c == '(' || c == ')')
                fputc('\\', _file);
            fputc(c, _file);

            pdf_X += charWidth; // update x position
        }
    }

    pdf_end_line();
    pdf_new_line();

    bdb.clear();
}

void colecoprinter::pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2)
{
    bdb.put(c);

    switch (c)
    {
    case 10: // LF
        Debug_printv("LF");
        line_output();
        break;
    case 11:
        if (bdb.getIsVT() == true)
        {
            line_output();
            bdb.setIsVT(false);
        }
        else
        {
            bdb.setIsVT(true);
        }
        break;
    case 12:
        pdf_end_page();
        break;
    case 13:
        bdb.reset();
        break;
    }
}

void colecoprinter::post_new_file()
{
    shortname = "a1027";

    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 66.0;
    topMargin = 32.0;
    bottomMargin = 48.0;
    printWidth = 480.0; // 6 2/3 inches
    lineHeight = 12.0;
    charWidth = 6.0; // 12cpi
    fontNumber = 1;
    fontSize = 10;

    bdb.clear();
    bdb.reset();

    pdf_header();
}
