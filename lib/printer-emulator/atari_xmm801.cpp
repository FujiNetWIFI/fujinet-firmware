#include "atari_xmm801.h"
#include "utils.h"
#include "../../include/debug.h"

void xmm801::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
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
#ifdef DEBUG
            Debug_printf("Command: %c\n", c);
#endif
        }
        else
        {
            epson_cmd.ctr++; // increment counter to keep track of the byte in the command
#ifdef DEBUG
            Debug_printf("Command counter: %d\n", epson_cmd.ctr);
#endif
        }

        if (epson_cmd.ctr == 1)
        {
            epson_cmd.N1 = c;
#ifdef DEBUG
            Debug_printf("N1: %d\n", c);
#endif
        }
        else if (epson_cmd.ctr == 2)
        {
            epson_cmd.N2 = c;
#ifdef DEBUG
            Debug_printf("N2: %d\n", c);
#endif
        }
        else if (epson_cmd.ctr == 3)
        {
            epson_cmd.N = (uint16_t)epson_cmd.N1 + 256 * ((uint16_t)(epson_cmd.N2 & (uint8_t)0x07));
#ifdef DEBUG
            Debug_printf("N: %d\n", epson_cmd.N);
#endif
        }
        // state machine actions
        switch (epson_cmd.cmd)
        {
        case 1: // XMM
        case 2: // XMM
        case 3: // XMM
        case 4: // XMM
        case 5: // XMM
        case 6: // XMM
            // forward dot spaces
            // need either 1-dot glyph or need to do a spacing adjustment like backspace

            if (epson_font_mask & fnt_proportional)
            {
                fprintf(_file, " )%d(", (int)(280 - epson_cmd.cmd * 40));
                pdf_X += 0.48 * (double)epson_cmd.cmd;
            }
            else if (epson_font_mask & fnt_compressed)
            {
                fprintf(_file, " )%d(", (int)(360 - epson_cmd.cmd * 40)); // need correct value for 16.7 CPI
                pdf_X += 0.48 * (double)epson_cmd.cmd;
            }
            else
            {
                fprintf(_file, " )%d(", (int)(600 - epson_cmd.cmd * 60)); // need correct value for 10 CPI
                pdf_X += 0.72 * (double)epson_cmd.cmd;
            }

            reset_cmd();
            break;
        case 10:                  // XMM                 // full reverse line feed
            pdf_dY += lineHeight; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            reset_cmd();
            break;
        case 14: // XMM // Double width printing.  SO wide behavior per page 27 of XMM801 manual
            set_mode(fnt_SOwide);
            reset_cmd();
            break;
        case 15: // XMM // double width OFF
            clear_mode(fnt_SOwide);
            clear_mode(fnt_expanded); // does both per page 28 of XMM801 manual
            reset_cmd();
            break;
        // case 17: // Proportional character set ON
        //     set_mode(fnt_proportional);
        //     clear_mode(fnt_compressed);
        //     reset_cmd();
        //     break;
        case 19: // XMM // Select 10 CPI
            clear_mode(fnt_proportional | fnt_compressed | fnt_elite);
            reset_cmd();
            break;
        case 20: // XMM // Turns on compressed mode.
            set_mode(fnt_compressed);
            clear_mode(fnt_proportional);
            reset_cmd();
            break;
        case 23: // XMM // international ON
            intlFlag = true;
            reset_cmd();
            break;
        case 24: // XMM // international OFF
            intlFlag = false;
            reset_cmd();
            break;
        case 25: // XMM // underline on
            set_mode(fnt_underline);
            reset_cmd();
            break;
        case 26: // XMM // underline off
            clear_mode(fnt_underline);
            reset_cmd();
            break;
        case 28:                       /// XMM                     // half forward line feed
            pdf_dY -= lineHeight / 2.; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            reset_cmd();
            break;
        case 30:                       // XMM              // half reverse line feed
            pdf_dY += lineHeight / 2.; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            reset_cmd();
            break;

        case '0': // XMM - turn off paper-out sensor
        case '/': // XMM - turn on paper-out sensor
            esc_not_implemented();
            reset_cmd();
            break;
        case '1': // XMM // 7/72" spacing
            lineHeight = 7;
            reset_cmd();
            break;
        case '6': // XMM //Returns line spacing to default of 1/6
            lineHeight = 72.0 / 6.0;
            reset_cmd();
            break;
        case '8': // XMM //Returns line spacing to default of 1/6
            lineHeight = 72.0 / 8.0;
            reset_cmd();
            break;
        case '3': // XMM // Sets line spacing to N/216". Stays on until changed
            if (epson_cmd.ctr > 0)
            {
                lineHeight = 72.0 * (double)epson_cmd.N1 / 216.0;
                reset_cmd();
            }
            break;
        // case '4': // Italic character set ON
        //     set_mode(fnt_italic);
        //     reset_cmd();
        //     break;
        // case '5': // Italic character set OFF
        //     clear_mode(fnt_italic);
        //     reset_cmd();
        //     break;
        // case '8':
        // case '9':
        case '<': // XMM
                  // case '=':
                  // case '>':
            esc_not_implemented();
            reset_cmd();
            break;
        case '@': // XMM // Resets all special modes to power up state including Top Of Form
                  // need to reset font to normal
                  // not sure what to do about TOF?
#ifdef DEBUG
            Debug_printf("@ reset!\n");
#endif
            at_reset();
            epson_set_font(epson_font_lookup(0), 7.2);
            reset_cmd();
            break;
        case 'A': // XMM //Sets spacing of LF (line feed) to N/72
            if (epson_cmd.ctr > 0)
            {
                lineHeight = epson_cmd.N1;
                reset_cmd();
            }
            break;
        case 'B': // XMM - vertical tab stops
            // can be up to 5, terminated in 0x00
            // this will cause error
            esc_not_implemented();
            reset_cmd();
            break;
        case 'C': // XMM
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
        // case 'D':
        // Reset current tabs and sets up to 28 HT (horiz tabs) ................. 9-4
        // TABs may range up to maximum width for character and
        // printer size. E.G. Maximum TAB for normal characters on
        // MX-80 is 80.
        // Format: <ESC>"D" NI N2 N3 ... NN 0.
        // Terminate TAB sequence with zero or 128.
        // esc_not_implemented();
        // if (((c & 0x7F) == 0) || (epson_cmd.ctr > 28))
        //     reset_cmd();
        // break;
        case 'E': // XMM // Turns on emphasized mode. Can't mix with superscript, subscript, or compressed modes
            set_mode(fnt_emphasized);
            reset_cmd();
            break;
        case 'F': // XMM // Turns off emphasized mode
            clear_mode(fnt_emphasized);
            reset_cmd();
            break;
        case 'G': // XMM // Turns on double strike mode.
            set_mode(fnt_doublestrike);
            reset_cmd();
            break;
        case 'H': // XMM // Turns off double strike mode, superscript, and subscript modes
            clear_mode(fnt_doublestrike | fnt_superscript | fnt_subscript);
            reset_cmd();
            break;
        case 'J': // XMM // Sets line spacing to N/216" for one line only and
                  // when received causes contents of buffer to print
                  // IMMEDIATE LINE FEED OF SIZE N/216
            if (epson_cmd.ctr > 0)
            {
                pdf_dY -= 72. * ((double)epson_cmd.N1) / 216.; // set pdf_dY and rise to N1/216.
                pdf_set_rise();
                reset_cmd();
            }
            break;
            //        case 'j': // FX-80 immediate reverse line feed just like 'J'
            // when received causes contents of buffer to print
            // IMMEDIATE REVERSE LINE FEED OF SIZE N/216
            // if (epson_cmd.ctr > 0)
            // {
            //     pdf_dY += 72. * (double)epson_cmd.N1 / 216.; // set pdf_dY and rise to N1/216.
            //     pdf_set_rise();
            //     reset_cmd();
            // }
            // break;
        case 'K': // XMM // Sets dot graphics mode to 480 dots per 8" line
        case 'V': // XMM // high density graphics
                  // case 'L': // Sets dot graphics mode to 960 dots per 8" line
                  // case 'Y': // on FX-80 this is double speed but with gotcha
                  // case 'Z': // quadruple density mode with gotcha
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
#ifdef DEBUG
                Debug_printf("Switch to GFX mode\n");
#endif
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
                // case 'L': // Sets dot graphics mode to 960 dots per 8" line
                case 'V': // XMM
                          // case 'Y': // on FX-80 this is double speed but with gotcha
                    charWidth = 0.6;
                    break;
                //case 'Z': // on FX-80 this is double speed but with gotcha
                //    charWidth = 0.3;
                //    break;
                default:
                    charWidth = 1.2;
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
                //case 'L': // Sets dot graphics mode to 960 dots per 8" line
                //case 'Y': // on FX-80 this is double speed but with gotcha
                case 'V': // XMM
                    fprintf(_file, ")66.5(");
                    break;
                    //case 'Z': // on FX-80 this is double speed but with gotcha
                    //    fprintf(_file, ")99.75(");
                    //    break;
                }
                //fprintf(_file, "]TJ [(");
                if (epson_cmd.ctr == (epson_cmd.N + 2))
                {
                    // reset font
                    epson_set_font(1, 7.2);
                    textMode = true;
                    reset_cmd();
#ifdef DEBUG
                    Debug_printf("Finished GFX mode\n");
#endif
                }
            }
            break;
        case 'M': // XMM // elite on
            set_mode(fnt_elite);
            reset_cmd();
            break;
        case 'N': // XMM // Sets skip over perforation to N lines
            esc_not_implemented();
            if (epson_cmd.ctr > 0)
            {
                reset_cmd();
            }
            break;
        // case 'O': // Resets skip over perforation to 0 lines
        //     esc_not_implemented();
        //     break;
        case 'P': // XMM // elite off
            clear_mode(fnt_elite);
            reset_cmd();
            break;
        case 'Q': //XMM // Sets column width to N
            // 1 < = N < = maximum number of characters/line.
            // width on MX80, right margin on FX80
            if (epson_cmd.ctr > 0)
            {
                // range check N1
                printWidth = epson_cmd.N1 * charWidth;
                reset_cmd();
            }
            break;
        case 'R': // XMM - reverse of epson
                  // case 'S': // Sets superscript/subscript modes
            // N=0 = > superscript, N>0 = > subscript.
            if (epson_cmd.ctr > 0)
            {
                if (epson_cmd.N1 != 0)
                    set_mode(fnt_superscript); // XMM
                else
                    set_mode(fnt_subscript); // XMM
                reset_cmd();
            }
            break;
        case 'T': // XMM // Resets superscript, subscript, and unidrectional printing
            // does not turn off double strike from script modes
            clear_mode(fnt_superscript | fnt_subscript);
            reset_cmd();
            break;
        case 'U': // XMM // Unidirectional printing. Prints each line from left to right
                  // N=0 = > OFF,N>0 = > ON.
            esc_not_implemented();
            if (epson_cmd.ctr > 0)
            {
                reset_cmd();
            }
            break;
        case 'W': // XMM // Double width printing. Stays ON until turned OFF
            // N=0 = > OFF, N=1 = > ON.
            // Has precedence over Shift Out (SO = CHR$(14))
            // Clears SO/14 mode
            // Looks like modulo 48 from FX Printer Manual
            if (epson_cmd.ctr > 0)
            {
                // Debug_printf("Double Width command, arg = %d\n", epson_cmd.N1);
                if ((epson_cmd.N1 % '0') != 0)
                    set_mode(fnt_expanded);
                else
                {
                    clear_mode(fnt_expanded);
                    clear_mode(fnt_SOwide);
                }
                reset_cmd();
            }
            break;
        case 'p': // XMM - proportional
            // N=0 = > OFF,N>0 = > ON.
            esc_not_implemented();
            if (epson_cmd.ctr > 0)
            {
                reset_cmd();
            }
            break;
        default:
            reset_cmd();
            break;
        }
    }
    else
    { // check for other commands or printable character
        // marked with XMM means verified per manaul
        switch (c)
        {
        case 7: // Sounds buzzer for 3 seconds. Paper out rings for 3 seconds
            // would be fun to make a buzzer
            // XMM
            if (intlFlag)
                break;
            break;
        case 8: // Backspace. Empties printer buffer, then backspaces print head one space
            if (intlFlag)
                break;
            /*MX Printer with GRAFTRAXplus Manual page 6-3:
            One quirk in using the backspace. In expanded mode, CHR$(8) causes a full double
            width backspace as we would expect. The fun begins when several backspaces
            are done in succession. All except for the first one are normal-width backspaces */
            fprintf(_file, ")%d(", (int)(charWidth / lineHeight * 900.));
            pdf_X -= charWidth; // update x position
            // XMM
            break;
        case 9: // Horizontal Tabulation. Print head moves to next tab stop
            if (intlFlag)
                break;
            not_implemented();
            // XMM
            break;
        case 10: // Line Feed. Printer empties its buffer and does line feed at
            if (intlFlag)
                break;
            // current line spacing and Resets buffer pointer to zero
            pdf_dY -= lineHeight; // set pdf_dY and rise to one line
            pdf_set_rise();
            // XMM
            break;
        case 11: //Vertical Tab - does single line feed (same as LF on MX80)
            if (intlFlag)
                break;
            not_implemented();
            // set pdf_dY -= lineHeight;
            // use rise feature in pdf: ")]TJ pdf_dY Ts [("
            // at CR(auto LF) do a: " 0 Ts \n 0 -lineHeight+pdf_dY Td" to reset the rise
            // set pdf_dY=0;
            // XMM
            break;
        case 12: // Advances paper to next logical TOF (top of form)
            if (intlFlag)
                break;
            pdf_end_page();
            // XMM
            break;
        case 13: // Carriage Return.
            if (intlFlag)
                break;
            // Prints buffer contents and resets buffer character count to zero
            // Implemented outside in pdf_printer()
            // XMM
            break;
        case 14: // Turns off underline
            if (intlFlag)
                break;
            clear_mode(fnt_underline);
            // XMM
            break;
        case 15: // Turns on underline
            if (intlFlag)
                break;
            set_mode(fnt_underline);
            // XMM
            break;
        // case 18: // Turns off compressed characters and empties buffer
        //     clear_mode(fnt_compressed);
        //     break;
        // case 20: // Turns off double width mode (14 only)
        //     clear_mode(fnt_SOwide);
        //     break;
        case 27: // ESC mode
            escMode = true;
            break;
        default: // maybe printable character
                 // TODO: need INTERNATIONAL CHARACTERS

            // adjust typeface font
            uint8_t new_F = epson_font_lookup(epson_font_mask);
            if (fontNumber != new_F)
            {
                double new_w = epson_font_width(epson_font_mask);
                epson_set_font(new_F, new_w);
            }

            //printable characters for 1025 Standard Set
            if (intlFlag && (c < 32 || c == 96 || c == 123 || c == 126 || c == 127))
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
                    // Codes 28-31 are arrows located at 28-31 + 160
                    d = c + 0xA0;
                    valid = true;
                }
                else
                    switch (c)
                    {
                    case 96:
                        d = uint8_t(161);
                        valid = true;
                        break;
                    case 123:
                        d = uint8_t(196);
                        valid = true;
                        break;
                    case 126:
                        d = uint8_t(182); // owner manual shows EOL ATASCII symbol
                        valid = true;
                        break;
                    case 127:
                        d = uint8_t(171); // owner manual show <| block arrow symbol
                        valid = true;
                        break;
                    default:
                        valid = false;
                        break;
                    }
                if (valid)
                {
                    fputc(d, _file);
                    pdf_X += charWidth; // update x position
                }
            }
            else if (c > 31 && c < 127)
            {
                if (c == '\\' || c == '(' || c == ')')
                    fputc('\\', _file);
                fputc(c, _file);
                pdf_X += charWidth; // update x position
            }
            // if (c > 31) // && c < 127)
            // {
            //     uint8_t new_F = epson_font_lookup(epson_font_mask);
            //     if (fontNumber != new_F)
            //     {
            //         double new_w = epson_font_width(epson_font_mask);
            //         epson_set_font(new_F, new_w);
            //     }
            //     if (c == '\\' || c == '(' || c == ')')
            //         fputc('\\', _file);
            //     fputc(c, _file);
            //     pdf_X += charWidth; // update x position
            // }
            break;
        }
    }
}

void xmm801::post_new_file()
{
    epson80::post_new_file();
    translate850 = false;
    _eol = ATASCII_EOL;
}
