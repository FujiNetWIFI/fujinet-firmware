#include "atari_1027.h"
#include "debug.h"

void atari1027::pdf_handle_char(byte c)
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
        if (c == 25)
            uscoreFlag = true;
        if (c == 26)
            uscoreFlag = false;
        if (c == 23)
            intlFlag = true;
        if (c == 24)
            intlFlag = false;
        escMode = false;
    }
    else if (c == 15)
        uscoreFlag = true;
    else if (c == 14)
        uscoreFlag = false;
    else if (c == 27)
        escMode = true;
    else
    { // maybe printable character
        //printable characters for 1027 Standard Set + a few more >123 -- see mapping atari on ATASCII
        if (intlFlag && (c < 32 || c == 123))
        {
            // not sure about ATASCII 96.
            // todo: Codes 28-31 are arrows and require the symbol font
            if (c < 27)
                _file.write(intlchar[c]);
            else if (c == 123)
                _file.write(byte(196));
            else if (c > 27 && c < 32)
            {
                _file.printf(")600(_"); // |^ -< -> |v
            }

            pdf_X += charWidth; // update x position
        }
        else if (c > 31 && c < 127)
        {
            if (c == '\\' || c == '(' || c == ')')
                _file.write('\\');
            _file.write(c);

            if (uscoreFlag)
                _file.printf(")600(_"); // close text string, backspace, start new text string, write _

            pdf_X += charWidth; // update x position
        }
    }
}

void atari1027::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);
    //paperType = PDF;

    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 18.0;
    bottomMargin = 0;
    printWidth = 576.0; // 8 inches
    lineHeight = 12.0;
    charWidth = 7.2;
    fontNumber = 1;
    fontSize = 12;

    pdf_header();

    fonts[0] = &F1;
    pdf_add_fonts(1);

    uscoreFlag = false;
    intlFlag = false;
    escMode = false;
}