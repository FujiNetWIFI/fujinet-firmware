#include "atari_1029.h"
#include "utils.h"
#include "../../include/debug.h"

void atari1029::not_implemented()
{
    uint8_t c = epson_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: %u %x %c\n", c, c, c);
}

void atari1029::esc_not_implemented()
{
    uint8_t c = epson_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: ESC %u %x %c\n", c, c, c);
}

void atari1029::reset_cmd()
{
    escMode = false;
    epson_cmd.cmd = 0;
    epson_cmd.ctr = 0;
    epson_cmd.N1 = 0;
    epson_cmd.N2 = 0;
}

void atari1029::set_mode(uint16_t m)
{
    epson_font_mask |= m;
}

void atari1029::clear_mode(uint16_t m)
{
    epson_font_mask &= ~m;
}

void atari1029::print_8bit_gfx(uint8_t c)
{
    // e.g., [(0)100(1)100(4)100(50)]TJ
    // lead with '0' to enter a space
    // then shift back with 133 and print each pin
    fprintf(_file, "0");
    for (int i = 0; i < 7; i++)
    {
        if ((c >> i) & 0x01)
            fprintf(_file, ")100(%u", i + 1);
    }
}

void atari1029::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
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
            epson_cmd.N =( (uint16_t)epson_cmd.N2 & (uint8_t)0x07) + 256 * (uint16_t)(epson_cmd.N1);
#ifdef DEBUG
            Debug_printf("N: %d\n", epson_cmd.N);
#endif
        }
        // state machine actions
        switch (epson_cmd.cmd)
        {
        case 14:                    // expanded
            set_mode(fnt_expanded); // expanded mode ON
            reset_cmd();
            break;
        case 15:
            clear_mode(fnt_expanded); // expanded mode OFF
            reset_cmd();
            break;
        case 23: // international mode
            intlFlag = true;
            reset_cmd();
            break;
        case 24:
            intlFlag = false;
            reset_cmd();
            break;
        case 25:                     // underline
            set_mode(fnt_underline); // underline mode ON
            reset_cmd();
            break;
        case 26:                       // underline
            clear_mode(fnt_underline); // underline mode OFF
            reset_cmd();
            break;
        case '6': // 1/8 inch spacing 9 pts
            lineHeight = 12;
            reset_cmd();
            break;
        case '9': // 7/72" spacing
            lineHeight = 8;
            reset_cmd();
            break;
        case 'A': // Sets dot graphics mode to 480 dots per 8" line
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
                charWidth = 1.2;
                fprintf(_file, ")]TJ /F5 12 Tf [("); // set font to GFX mode
                fontUsed[4] = true;
            }

            if (epson_cmd.ctr > 2)
            {
                print_8bit_gfx(c);
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
        default:
            reset_cmd();
            break;
        }
    }
    else
    { // maybe printable character
        if (c == 27)
            escMode = true;
        else
        {
            uint8_t new_F = epson_font_lookup(epson_font_mask);
            if (fontNumber != new_F)
            {
                double new_w = epson_font_width(epson_font_mask);
                epson_set_font(new_F, new_w);
            }
            //printable characters for 1027 Standard Set + a few more >123 -- see mapping atari on ATASCII
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
                        d = uint8_t(182); // service manual shows EOL ATASCII symbol
                        valid = true;
                        break;
                    case 127:
                        d = uint8_t(171); // service manual show <| block arrow symbol
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
        }
    }
}

uint8_t atari1029::epson_font_lookup(uint16_t code)
{
    // four fonts:

    //
    return code + 1;
}

double atari1029::epson_font_width(uint16_t code)
{
    if (code & fnt_expanded)
        return 14.4;
    else
        return 7.2; // 10 cpi
}

void atari1029::epson_set_font(uint8_t F, double w)
{
    fprintf(_file, ")]TJ /F%u 12 Tf [(", F);
    charWidth = w;
    fontNumber = F;
    fontUsed[F - 1] = true;
}

void atari1029::pdf_clear_modes()
{
    clear_mode(fnt_expanded | fnt_underline);
}

void atari1029::post_new_file()
{
    translate850 = false;
    _eol = ATASCII_EOL;

    shortname = "a1029";

    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 18.0;
    bottomMargin = 0;
    printWidth = 576.0; // 8 inches
    lineHeight = 12.0;
    charWidth = 7.2;
    fontNumber = 1;
    fontSize = 12;
    // pdf_dY = lineHeight;

    pdf_header();
    escMode = false;
}
