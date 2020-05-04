#include "atari_1025.h"
#include "debug.h"

void atari1025::pdf_handle_char(byte c)
{
    if (escMode)
    {
        // Atari 1025 escape codes:
        // ESC CTRL-T - 16.5 char/inch        0x14
        // ESC CTRL-O - 10 char/inch          0x0F
        // ESC CTRL-N - 5 char/inch           0x0E
        // ESC L - long line 80 char/line     0x4C
        // ESC S - short line 64 char/line    0x53
        // ESC 6 - use 6 lines per inch       0x36
        // ESC 8 - use 8 lines per inch       0x38
        // ESC CTRL-W - start international   0x17 23
        // ESC CTRL-X - stop international    0x18 24

        switch (c)
        {
        case 0x0E:
            // change font to elongated like
            if (fontNumber != 2)
            {
                _file.printf(")]TJ\n/F2 12 Tf [(");
                charWidth = 14.4; //72.0 / 5.0;
                fontNumber = 2;
            }
            break;
        case 0x0F:
            // change font to normal
            if (fontNumber != 1)
            {
                _file.printf(")]TJ\n/F1 12 Tf [(");
                charWidth = 7.2; //72.0 / 10.0;
                fontNumber = 1;
            }
            break;
        case 0x14:
            // change font to compressed
            if (fontNumber != 3)
            {
                _file.printf(")]TJ\n/F3 12 Tf [(");
                charWidth = 72.0 / 16.5;
                fontNumber = 3;
            }
            break;
        case 0x17: // 23
            intlFlag = true;
            break;
        case 0x18: // 24
            intlFlag = false;
            break;
        case 0x36:             // '6'
            lineHeight = 12.0; //72.0/6.0;
            break;
        case 0x38:            // '8'
            lineHeight = 9.0; //72.0/8.0;
            break;
        case 0x4c: // 'L'
            /* code */
            // for long and short lines, i think we end line, ET, then set the leftMargin and pageWdith and begin text
            // challenge is to not skip a line if we're at the beginning of a line
            // could also add a state variable so we don't unnecessarily change the line width
            if (shortFlag)
            {
                if (!BOLflag)
                    pdf_end_line();   // close out string array
                _file.printf("ET\n"); // close out text object
                // set new margins
                leftMargin = 18.0;  // (8.5-8.0)/2*72
                printWidth = 576.0; // 8 inches
                pdf_begin_text(pdf_Y);
                // start text string array at beginning of line
                _file.printf("[(");
                BOLflag = false;
                shortFlag = false;
            }
            break;
        case 0x53: // 'S'
            // for long and short lines, i think we end line, ET, then set the leftMargin and pageWdith and begin text
            if (!shortFlag)
            {
                if (!BOLflag)
                    pdf_end_line();   // close out string array
                _file.printf("ET\n"); // close out text object
                // set new margins
                leftMargin = 75.6;  // (8.5-6.4)/2.0*72.0;
                printWidth = 460.8; //6.4*72.0; // 6.4 inches
                pdf_begin_text(pdf_Y);
                // start text string array at beginning of line
                _file.printf("[(");
                BOLflag = false;
                shortFlag = true;
            }
            break;
        default:
            break;
        }

        escMode = false;
    }
    else if (c == 27)
        escMode = true;
    else
    { // maybe printable character
        //printable characters for 1027 Standard Set + a few more >123 -- see mapping atari on ATASCII
        if (intlFlag && (c < 32 || c == 96 || c == 123))
        {
            // not sure about ATASCII 96.
            // todo: Codes 28-31 are arrows
            if (c < 28)
                _file.write(intlchar[c]);
            else if (c < 32)
                _file.write(c);
            else if (c == 96)
                _file.write(byte(161));
            else if (c == 123)
                _file.write(byte(196));

            pdf_X += charWidth; // update x position
        }
        else if (c > 31 && c < 127)
        {
            if (c == '\\' || c == '(' || c == ')')
                _file.write('\\');
            _file.write(c);

            // if (uscoreFlag)
            //     _file.printf(")600(_"); // close text string, backspace, start new text string, write _

            pdf_X += charWidth; // update x position
        }
    }
}

void atari1025::initPrinter(FS *filesystem)
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

    // uscoreFlag = false;
    intlFlag = false;
    escMode = false;
}