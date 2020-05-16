#include "epson_80.h"
#include "../../include/debug.h"

void epson80::pdf_handle_char(byte c)
{
    if (escMode)
    {
        switch (c)
        {

        case 0x0E:
            // change font to elongated like
            if (fontNumber != 2)
            {
                _file.printf(")]TJ\n/F2 12 Tf [(");
                charWidth = 14.4; //72.0 / 5.0;
                fontNumber = 2;
                fontUsed[1] = true;
            }
            break;
        case 0x0F:
            // change font to normal
            if (fontNumber != 1)
            {
                _file.printf(")]TJ\n/F1 12 Tf [(");
                charWidth = 7.2; //72.0 / 10.0;
                fontNumber = 1;
                // fontUsed[0]=true; // redundant
            }
            break;
        case 0x14:
            // change font to compressed
            if (fontNumber != 3)
            {
                _file.printf(")]TJ\n/F3 12 Tf [(");
                charWidth = 72.0 / 16.5;
                fontNumber = 3;
                fontUsed[2] = true;
            }
            break;
        case 0x17: // 23
            //intlFlag = true;
            break;
        case 0x18: // 24
            //intlFlag = false;
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
            /* if (shortFlag)
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
            } */
            break;
        case 0x53: // 'S'
            // for long and short lines, i think we end line, ET, then set the leftMargin and pageWdith and begin text
            /* if (!shortFlag)
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
            } */
            break;
        default:
            break;
        }

        escMode = false;
    }
    else
    { // maybe printable character
        switch (c)
        {
        case 7: // BEL
            // would be fun to make a buzzer
            break;
        case 8: // BS
            // back space
            _file.printf(")%d(", charWidth / lineHeight * 1000); // need to figure out charwidth
            pdf_X -= charWidth;                                  // update x position
            break;
        case 9: // TAB
// not implemented
#ifdef DEBUG
            Debug_printf("command not implemented: %d\n", c);
#endif
            break;
        case 10: // LF
        case 11: // VT (same as LF on MX80)
#ifdef DEBUG
            Debug_printf("command not implemented: %d\n", c);
#endif
            break;
        case 12: // FF
            pdf_end_page();
            break;
        //case 13: // CR - implemented outside in pdf_printer()
        case 14: // double width mode ON
            // set double width bit in font mode code
            break;
        case 15: // compressed mode
            // set compresed mode bit in font code
            break;
        case 18: // turns off compressed
            break;
        case 20: // turns off double width
            break;
        case 27: // ESC mode
            escMode = true;
            break;
        default:
            if (c > 31 && c < 127)
            {
                if (c == '\\' || c == '(' || c == ')')
                    _file.write('\\');
                _file.write(c);
                pdf_X += charWidth; // update x position
            }
            break;
        }
    }
// update font?
}

void epson80::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);

    translate850 = true;
    _eol = ASCII_CR;

    shortname = "epson";

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
    escMode = false;
}