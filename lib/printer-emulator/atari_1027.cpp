#include "atari_1027.h"

#include "../../include/debug.h"


void atari1027::pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2)
{
    if (escMode)
    {
        // Atari 1027 escape codes:
        // CTRL-O - start underscoring        15
        // CTRL-N - stop underscoring         14  - note in T1027.BAS there is a case of 27 14
        // ESC CTRL-Y - start underscoring    27  25
        // ESC CTRL-Z - stop underscoring     27  26
        // ESC CTRL-W - start international   27  23
        // ESC CTRL-X - stop international    27  24
        switch (c)
        {
        case 25:
            uscoreFlag = true;
            break;
        case 26:
            uscoreFlag = false;
            break;
        case 23:
            intlFlag = true;
            break;
        case 24:
            intlFlag = false;
            break;
        default:
            break;
        }
        escMode = false;
    }
    else if (!intlFlag && c == 15)
        uscoreFlag = true;
    else if (!intlFlag && c == 14)
        uscoreFlag = false;
    else if (c == 27)
        escMode = true;
    else if (c == 12)
    {
        pdf_end_page();
        pdf_new_page();
    }
    else
    { // maybe printable character
        //printable characters for 1027 Standard Set + a few more >123 -- see mapping atari on ATASCII
        if (intlFlag && (c < 32 || c == 96 || c == 123))
        {
            bool valid = false;
            uint8_t d = 0;

            if (c < 27)
            {
                d = intlchar[c];
                valid = true;
            }
            else if (c > 27 && c < 32)
            {
                // Codes 28-31 are arrows made from compound chars
                uint8_t d1 = (uint8_t)'|';
                switch (c)
                {
                case 28:
                    d = (uint8_t)'^';
                    break;
                case 29:
                    d = (uint8_t)'v';
                    d1 = (uint8_t)'!';
                    break;
                case 30:
                    d = (uint8_t)'<';
                    d1 = (uint8_t)'-';
                    break;
                case 31:
                    d = (uint8_t)'>';
                    d1 = (uint8_t)'-';
                    break;
                default:
                    break;
                }
                fputc(d1, _file);
                fprintf(_file, ")600("); // |^ -< -> !v
                valid = true;
            }
            else
                switch (c)
                {
                case 96:
                    d = uint8_t(206); // use I with carot but really I with circle
                    valid = true;
                    break;
                case 123:
                    d = uint8_t(196);
                    valid = true;
                    break;
                default:
                    valid = false;
                    break;
                }
            if (valid)
            {
                fputc(d, _file);
                if (uscoreFlag)
                    fprintf(_file, ")600(_"); // close text string, backspace, start new text string, write _

                pdf_X += charWidth; // update x position
            }
        }
        else if (c > 31 && c < 128)
        {
            if (c == 123 || c == 125 || c == 127)
                c = ' ';
            if (c == '\\' || c == '(' || c == ')')
                fputc('\\', _file);
            fputc(c, _file);

            if (uscoreFlag)
                fprintf(_file, ")600(_"); // close text string, backspace, start new text string, write _

            pdf_X += charWidth; // update x position
        }
    }
}

void atari1027::post_new_file()
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

    uscoreFlag = false;
    intlFlag = false;
    escMode = false;
}
