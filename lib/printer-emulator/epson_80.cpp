#include "epson_80.h"

#include "../../include/debug.h"

#include "utils.h"


void epson80::not_implemented()
{
    uint8_t c = epson_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: %u 0x%x %c\r\n", c, c, c);
}

void epson80::esc_not_implemented()
{
    uint8_t c = epson_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: ESC %u 0x%x %c\r\n", c, c, c);
}

void epson80::reset_cmd()
{
    escMode = false;
    epson_cmd.cmd = 0;
    epson_cmd.ctr = 0;
    epson_cmd.N1 = 0;
    epson_cmd.N2 = 0;
}

void epson80::set_mode(uint16_t m)
{
    epson_font_mask |= m;
}

void epson80::clear_mode(uint16_t m)
{
    epson_font_mask &= ~m;
}

void epson80::print_8bit_gfx(uint8_t c)
{
    // e.g., [(0)100(1)100(4)100(50)]TJ
    // lead with '0' to enter a space
    // then shift back with 133 and print each pin
    fprintf(_file, "0");
    for (unsigned i = 0; i < 8; i++)
    {
        if ((c >> i) & 0x01)
            fprintf(_file, ")133(%u", i + 1);
    }
}

void epson80::pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2)
{
    if (escMode)
    {
        // command state machine switching
        if (epson_cmd.cmd == 0)
        {
            epson_cmd.ctr = 0;
            // epson_cmd.N1 = 0;
            // epson_cmd.N2 = 0;
            epson_cmd.cmd = c; // assign command char
            Debug_printf("Command: %c\r\n", c);
        }
        else
        {
            epson_cmd.ctr++; // increment counter to keep track of the byte in the command
            Debug_printf("Command counter: %d\r\n", epson_cmd.ctr);
        }

        if (epson_cmd.ctr == 1)
        {
            epson_cmd.N1 = c;
            Debug_printf("N1: %d\r\n", c);
        }
        else if (epson_cmd.ctr == 2)
        {
            epson_cmd.N2 = c;
            Debug_printf("N2: %d\r\n", c);
        }
        else if (epson_cmd.ctr == 3)
        {
            epson_cmd.N = (uint16_t)epson_cmd.N1 + 256 * ((uint16_t)(epson_cmd.N2 & (uint8_t)0x07));
            Debug_printf("N: %d\r\n", epson_cmd.N);
        }
        // state machine actions
        switch (epson_cmd.cmd)
        {
        case '#': // accept 8th bit "as is"
            esc_not_implemented();
            break;
        case '-': // underline
            if (epson_cmd.ctr > 0)
            {
                if (epson_cmd.N1 != 0)
                    set_mode(fnt_underline); // underline mode ON
                else
                    clear_mode(fnt_underline); // underline mode OFF
                reset_cmd();
            }
            break;
        case '0': // 1/8 inch spacing 9 pts
            lineHeight = 72 / 8;
            reset_cmd();
            break;
        case '1': // 7/72" spacing
            lineHeight = 7;
            reset_cmd();
            break;
        case '2': //Returns line spacing to default of 1/6
            lineHeight = 72 / 6;
            reset_cmd();
            break;
        case '3': // Sets line spacing to N/216". Stays on until changed
            if (epson_cmd.ctr > 0)
            {
                lineHeight = 72.0 * (double)epson_cmd.N1 / 216.0;
                reset_cmd();
            }
            break;
        case '4': // Italic character set ON
            set_mode(fnt_italic);
            reset_cmd();
            break;
        case '5': // Italic character set OFF
            clear_mode(fnt_italic);
            reset_cmd();
            break;
        case '8':
        case '9':
        case '<':
        case '=':
        case '>':
            esc_not_implemented();
            reset_cmd();
            break;
        case '@': // Resets all special modes to power up state including Top Of Form
                  // need to reset font to normal
                  // not sure what to do about TOF?
            Debug_printf("@ reset!\r\n");
            at_reset();
            epson_set_font(epson_font_lookup(0), 7.2);
            reset_cmd();
            break;
        case 'A': //Sets spacing of LF (line feed) to N/72
            if (epson_cmd.ctr > 0)
            {
                lineHeight = epson_cmd.N1;
                reset_cmd();
            }
            break;
        case 'C':
            // Sets form length (FL) to N lines. Default is 66
            // Format: <ESC>"C" N, 1 < = N < = 127.
            // Resets top of form.
            // Sets form length (FL) to N inches. Default is 11
            // Format: <ESC>"C" 0 N, 1 < = N < = 22.
            // Resets top of form.
            esc_not_implemented();
            if (epson_cmd.ctr == 1 && epson_cmd.N1 > 0)
                reset_cmd();
            else if (epson_cmd.ctr == 2)
                reset_cmd();
            break;
        case 'D':
            // Reset current tabs and sets up to 28 HT (horiz tabs) ................. 9-4
            // TABs may range up to maximum width for character and
            // printer size. E.G. Maximum TAB for normal characters on
            // MX-80 is 80.
            // Format: <ESC>"D" NI N2 N3 ... NN 0.
            // Terminate TAB sequence with zero or 128.
            esc_not_implemented();
            if (((c & 0x7F) == 0) || (epson_cmd.ctr > 28))
                reset_cmd();
            break;
        case 'E': // Turns on emphasized mode. Can't mix with superscript, subscript, or compressed modes
            set_mode(fnt_emphasized);
            reset_cmd();
            break;
        case 'F': // Turns off emphasized mode
            clear_mode(fnt_emphasized);
            reset_cmd();
            break;
        case 'G': // Turns on double strike mode.
            set_mode(fnt_doublestrike);
            reset_cmd();
            break;
        case 'H': // Turns off double strike mode, superscript, and subscript modes
            clear_mode(fnt_doublestrike | fnt_superscript | fnt_subscript);
            reset_cmd();
            break;
        case 'J': // Sets line spacing to N/216" for one line only and
                  // when received causes contents of buffer to print
                  // IMMEDIATE LINE FEED OF SIZE N/216
            if (epson_cmd.ctr > 0)
            {
                pdf_dY -= 72. * ((double)epson_cmd.N1) / 216.; // set pdf_dY and rise to N1/216.
                pdf_set_rise();
                reset_cmd();
            }
            break;
        case 'j': // FX-80 immediate reverse line feed just like 'J'
                  // when received causes contents of buffer to print
                  // IMMEDIATE REVERSE LINE FEED OF SIZE N/216
            if (epson_cmd.ctr > 0)
            {
                pdf_dY += 72. * (double)epson_cmd.N1 / 216.; // set pdf_dY and rise to N1/216.
                pdf_set_rise();
                reset_cmd();
            }
            break;
        case 'K': // Sets dot graphics mode to 480 dots per 8" line
        case 'L': // Sets dot graphics mode to 960 dots per 8" line
        case 'Y': // on FX-80 this is double speed but with gotcha
        case 'Z': // quadruple density mode with gotcha
            /* 
               Format: <ESC>"K" Nl N2, N1 and N2 determine line length.
               Line length = N1 +. 256*N2.
               1 < = N1 < = 255.
               0 < = N2 < = 255 (Modulo 8, i.e. 8 = 0) 

               Y&Z mode gotcha - store lastchar and print c&~lastchar
            */

            if (epson_cmd.ctr == 0)
            {
                textMode = false;
                Debug_printf("Switch to GFX mode\r\n");
            } // first change fonts to GFX font
            // then print GFX for each ctr value > 2
            // finally change fonts back to whatever it was for ctr == N+2
            if (epson_cmd.ctr == 2)
            {
                switch (epson_cmd.cmd)
                {
                case 'K':
                    charWidth = 1.2;
                    break;
                case 'L': // Sets dot graphics mode to 960 dots per 8" line
                case 'Y': // on FX-80 this is double speed but with gotcha
                    charWidth = 0.6;
                    break;
                case 'Z': // on FX-80 this is double speed but with gotcha
                    charWidth = 0.3;
                    break;
                }
                fprintf(_file, ")]TJ /F%d 9 Tf 100 Tz [(", NUMFONTS); // set font to GFX mode
                fontUsed[NUMFONTS - 1] = true;
            }

            if (epson_cmd.ctr > 2)
            {
                print_8bit_gfx(c);
                switch (epson_cmd.cmd)
                {
                case 'K':
                    break;
                case 'L': // Sets dot graphics mode to 960 dots per 8" line
                case 'Y': // on FX-80 this is double speed but with gotcha
                    fprintf(_file, ")66.5(");
                    break;
                case 'Z': // on FX-80 this is double speed but with gotcha
                    fprintf(_file, ")99.75(");
                    break;
                }
                //fprintf(_file, "]TJ [(");
                if (epson_cmd.ctr == (epson_cmd.N + 2))
                {
                    // reset font
                    epson_set_font(1, 7.2);
                    textMode = true;
                    reset_cmd();
                    Debug_printf("Finished GFX mode\r\n");
                }
            }
            break;
        case 'M': // elite on
            set_mode(fnt_elite);
            reset_cmd();
            break;
        case 'N': // Sets skip over perforation to N lines
            esc_not_implemented();
            if (epson_cmd.ctr > 0)
            {
                reset_cmd();
            }
            break;
        case 'O': // Resets skip over perforation to 0 lines
            esc_not_implemented();
            break;
        case 'P': // elite off
            clear_mode(fnt_elite);
            reset_cmd();
            break;
        case 'Q': // Sets column width to N
            // 1 < = N < = maximum number of characters/line.
            // width on MX80, right margin on FX80
            if (epson_cmd.ctr > 0)
            {
                // range check N1
                printWidth = epson_cmd.N1 * charWidth;
                reset_cmd();
            }
            break;
        case 'S': // Sets superscript/subscript modes
            // N=0 = > superscript, N>0 = > subscript.
            if (epson_cmd.ctr > 0)
            {
                if (epson_cmd.N1 != 0)
                    set_mode(fnt_subscript);
                else
                    set_mode(fnt_superscript);
                reset_cmd();
            }
            break;
        case 'T': // Resets superscript, subscript, and unidrectional printing
            // does not turn off double strike from script modes
            clear_mode(fnt_superscript | fnt_subscript);
            reset_cmd();
            break;
        case 'U': // Unidirectional printing. Prints each line from left to right
                  // N=0 = > OFF,N>0 = > ON.
            esc_not_implemented();
            if (epson_cmd.ctr > 0)
            {
                reset_cmd();
            }
            break;
        case 'W': // Double width printing. Stays ON until turned OFF
            // N=0 = > OFF, N=1 = > ON.
            // Has precedence over Shift Out (SO = CHR$(14))
            // Clears SO/14 mode
            // Looks like modulo 48 from FX Printer Manual
            if (epson_cmd.ctr > 0)
            {
                if (epson_cmd.N1 != 0)
                    set_mode(fnt_expanded);
                else
                {
                    clear_mode(fnt_expanded);
                    clear_mode(fnt_SOwide);
                }
                reset_cmd();
            }
            break;
        case 'l': // left margin on FX-80
                  // would need to restart the text object like on the Atari 1025 emulator
                  // or keep track of a pdf_dX variable to make adjustments in relative
                  // X-coordinates. For example:
                  // 0 -18 Td
                  // then adjust left margin
                  // pdf_dX (new left margin) -18 Td
                  // 0 -18 Td (just another line)
                  // then adjust left margin back to 0
                  // -pdf_dX -18 Td (have to shift it back to left)
                  // 0 -18 Td (just another line)
            esc_not_implemented();
            if (epson_cmd.ctr > 0)
            {
                // range check N1
                // printWidth = epson_cmd.N1 * charWidth;
                reset_cmd();
            }
            break;

            //        case 0x4c: // 'L'
            /* code */
            // for long and short lines, i think we end line, ET, then set the leftMargin and pageWdith and begin text
            // challenge is to not skip a line if we're at the beginning of a line
            // could also add a state variable so we don't unnecessarily change the line width
            /* if (shortFlag)
            {
                if (!BOLflag)
                    pdf_end_line();   // close out string array
                fprintf(_file,"ET\r\n"); // close out text object
                // set new margins
                leftMargin = 18.0;  // (8.5-8.0)/2*72
                printWidth = 576.0; // 8 inches
                pdf_begin_text(pdf_Y);
                // start text string array at beginning of line
                fprintf(_file,"[(");
                BOLflag = false;
                shortFlag = false;
            } */
            //            break;
            //        case 0x53: // 'S'
            // for long and short lines, i think we end line, ET, then set the leftMargin and pageWdith and begin text
            /* if (!shortFlag)
            {
                if (!BOLflag)
                    pdf_end_line();   // close out string array
                fprintf(_file,"ET\r\n"); // close out text object
                // set new margins
                leftMargin = 75.6;  // (8.5-6.4)/2.0*72.0;
                printWidth = 460.8; //6.4*72.0; // 6.4 inches
                pdf_begin_text(pdf_Y);
                // start text string array at beginning of line
                fprintf(_file,"[(");
                BOLflag = false;
                shortFlag = true;
            } */
            //            break;
        default:
            reset_cmd();
            break;
        }
    }
    else
    { // check for other commands or printable character
        switch (c)
        {
        case 7: // Sounds buzzer for 3 seconds. Paper out rings for 3 seconds
            // would be fun to make a buzzer
            break;
        case 8: // Backspace. Empties printer buffer, then backspaces print head one space
            /*MX Printer with GRAFTRAXplus Manual page 6-3:
            One quirk in using the backspace. In expanded mode, CHR$(8) causes a full double
            width backspace as we would expect. The fun begins when several backspaces
            are done in succession. All except for the first one are normal-width backspaces */
            fprintf(_file, ")%d(", (int)(charWidth / lineHeight * 900.));
            pdf_X -= charWidth; // update x position
            break;
        case 9: // Horizontal Tabulation. Print head moves to next tab stop
            not_implemented();
            break;
        case 10: // Line Feed. Printer empties its buffer and does line feed at
                 // current line spacing and Resets buffer pointer to zero
            pdf_dY -= lineHeight; // set pdf_dY and rise to one line
            pdf_set_rise();
            break;
        case 11: //Vertical Tab - does single line feed (same as LF on MX80)
            not_implemented();
            // set pdf_dY -= lineHeight;
            // use rise feature in pdf: ")]TJ pdf_dY Ts [("
            // at CR(auto LF) do a: " 0 Ts \n 0 -lineHeight+pdf_dY Td" to reset the rise
            // set pdf_dY=0;
            break;
        case 12: // Advances paper to next logical TOF (top of form)
            pdf_end_page();
            pdf_new_page();
            break;
        case 13: // Carriage Return.
            // Prints buffer contents and resets buffer character count to zero
            // Implemented outside in pdf_printer()
            break;
        case 14: // Turns on double width mode to end of line unless cancelled by 20
            set_mode(fnt_SOwide);
            break;
        case 15: // Turns on compressed character mode. Does not work with
                 // emphasized mode. Stays on until cancelled by OC2 (18)
            set_mode(fnt_compressed);
            break;
        case 18: // Turns off compressed characters and empties buffer
            clear_mode(fnt_compressed);
            break;
        case 20: // Turns off double width mode (14 only)
            clear_mode(fnt_SOwide);
            break;
        case 27: // ESC mode
            escMode = true;
            break;
        default:        // maybe printable character
            if (c > 31) // && c < 127)
            {
                uint8_t new_F = epson_font_lookup(epson_font_mask);
                if (fontNumber != new_F)
                {
                    double new_w = epson_font_width(epson_font_mask);
                    epson_set_font(new_F, new_w);
                }
                if (c == '\\' || c == '(' || c == ')')
                    fputc('\\', _file);
                fputc(c, _file);
                pdf_X += charWidth; // update x position
            }
            break;
        }
    }
}

uint8_t epson80::epson_font_lookup(uint16_t code)
{
    // uint16_t mask = 0x0FFF;
    
    /**
      * Table G-3 Mode Priorities (FX Manual Vol 2)
      * elite
      * proportional
      * emphasized (bold)
      * compressed
      * pica
      * 
      * proportional are always emphasized
      * script are always doublestrike
      * 
      * */

    // only support some basic font features 
    uint16_t mask = fnt_elite | fnt_compressed | fnt_italic | fnt_expanded | fnt_doublestrike | fnt_underline;

     if (code & fnt_elite)
     {
         mask &= ~(fnt_proportional | fnt_emphasized | fnt_compressed);
     }
     else if (code & fnt_proportional)
     {
         mask &= ~(fnt_emphasized | fnt_compressed);
     } 
     else if (code & fnt_emphasized)
     {
         mask &= ~fnt_compressed;
     }
     
    if (code & (fnt_subscript | fnt_superscript))
    {
        mask &= ~(fnt_doublestrike);
    }

    if (code & fnt_SOwide)
        code |= fnt_expanded;

    uint8_t index = 0;
    while (index < NUMFONTS - 1)
    {
        if ((code & mask) == (font_tab[index] & mask))
            return index + 1;
        index++;
    }
    Debug_printf("Epson 80 cannot find font %d\r\n", mask & code);
    return 1; // return the default if can't make sense
}

double epson80::epson_font_width(uint16_t code)
{
    // compute font width from code
    double width = 7.2; // default pica
    /**
      * Table G-3 Mode Priorities (FX Manual Vol 2)
      * elite
      * proportional
      * emphasized (bold)
      * compressed
      * pica
      * */

     if (code & fnt_elite)
     {
         width = 6.0; // 12 CPI
     }
    //  else if (code & fnt_proportional)
    //  {
    //      width = 5.0; // average prop width for now?
    //  } 
     else if (code & fnt_compressed)
     {
         width = 576. / 132.; // double check
     }

    if (code & fnt_expanded)
        width *= 2.0;

    return width;
}

void epson80::epson_set_font(uint8_t F, double w)
{
    fprintf(_file, ")]TJ /F%u 9 Tf 120 Tz [(", F);
    charWidth = w;
    fontNumber = F;
    fontUsed[F - 1] = true;
}

void epson80::pdf_clear_modes()
{
    clear_mode(fnt_SOwide);
//    clear_mode(fnt_expanded | fnt_underline);
}

void epson80::at_reset()
{
    leftMargin = 18.0;
#ifndef BUILD_APPLE
    bottomMargin = 0;
#else
    bottomMargin = 13; // line height + 1
#endif /* APPLE2 */
    printWidth = 576.0; // 8 inches
    lineHeight = 12.0;
    charWidth = 7.2;
    fontNumber = 1;
    fontSize = 9;
    fontHorizScale = 120;
    textMode = true;
    epson_font_mask = 0;
}

void epson80::post_new_file()
{
    translate850 = true;
    _eol = ASCII_CR;

    shortname = "epson";

    pageWidth = 612.0;
    pageHeight = 792.0;
#ifndef BUILD_APPLE
    topMargin = 16.0;
#else
    topMargin = 12;
#endif /* APPLE2 */
    // leftMargin = 18.0;
    // bottomMargin = 0;
    // printWidth = 576.0; // 8 inches
    // lineHeight = 12.0;
    // charWidth = 7.2;
    // fontNumber = 1;
    // fontSize = 12;
    at_reset(); // moved all those parameters so could be excuted with ESC-@ command
    // pdf_dY = lineHeight; // now in epson_tps.cpp

    pdf_header();
    escMode = false;
}
