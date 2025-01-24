#include "atari_820.h"

void atari820::post_new_file()
{
    shortname = "a820";

    pageWidth = 279.0;  // paper roll is 3 7/8" from page 6 of owners manual
    pageHeight = 792.0; // just use 11" for letter paper
    leftMargin = 19.5;  // fit print width on page width
    bottomMargin = 0.0;
    // dimensions from Table 1-1 of Atari 820 Field Service Manual
    printWidth = 240.0; // 3 1/3" wide printable area
    lineHeight = 12.0;  // 6 lines per inch
    charWidth = 6.0;    // 12 char per inch
    fontNumber = 1;
    fontSize = 12;
    sideFlag = false;

    pdf_header();
}

void atari820::pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2)
{
    // Atari 820 modes:
    // aux1 == 40   normal mode
    // aux1 == 29   sideways mode
    if (aux1 == 'N' && sideFlag)
    {
        fprintf(_file, ")]TJ\n/F1 12 Tf [(");
        fontNumber = 1;
        fontSize = 12;
        sideFlag = false;
    }
    else if (aux1 == 'S' && !sideFlag)
    {
        fprintf(_file, ")]TJ\n/F2 12 Tf [(");
        fontNumber = 2;
        fontSize = 12;
        sideFlag = true;
        fontUsed[1] = true;
        // could increase charWidth, but not necessary to make this work. I force EOL.
    }

    if (c == 12)
    {
        pdf_end_page();
        pdf_new_page();
    }

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
