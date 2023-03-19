#include "commodoremps803.h"

void commodoremps803::post_new_file()
{
    shortname = "mps803";

    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 18.0;
    bottomMargin = 0;
    printWidth = 576.0; // 8 inches
    lineHeight = 12.0;
    charWidth = 7.2;
    fontNumber = 1;
    fontSize = 12;
    pdf_dY = lineHeight;

    pdf_header();
}

void commodoremps803::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
{
    // maybe printable character
    if (c > 31 && c < 127)
    {
        if (!sideFlag || c > 47)
        {
            if (c == ('\\') || c == '(' || c == ')')
                fwrite("\\", 1, 1, _file);
            fwrite(&c, 1, 1, _file);
        }
        else
        {
            if (c < 48)
                fwrite(" ", 1, 1, _file);
        }

        pdf_X += charWidth; // update x position
    }
}
