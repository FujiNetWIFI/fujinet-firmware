#include "atari_825.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

void atari825::not_implemented()
{
    uint8_t c = epson_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: %u %x %c\n", c, c, c);
}

void atari825::esc_not_implemented()
{
    uint8_t c = epson_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: ESC %u %x %c\n", c, c, c);
}

void atari825::reset_cmd()
{
    escMode = false;
    epson_cmd.cmd = 0;
    epson_cmd.ctr = 0;
    epson_cmd.N1 = 0;
    epson_cmd.N2 = 0;
}

void atari825::set_mode(uint16_t m)
{
    epson_font_mask |= m;
}

void atari825::clear_mode(uint16_t m)
{
    epson_font_mask &= ~m;
}

void atari825::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
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
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            // forward dot spaces
            // need either 1-dot glyph or need to do a spacing adjustment like backspace
            if (epson_cmd.ctr > 0)
            {
                pdf_X += 1.2 * (float)c;
                reset_cmd();
            }
            break;
        case 10:                  // full reverse line feed
            pdf_dY += lineHeight; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            reset_cmd();
            break;
        case 14: // Double width printing. Stays ON until turned OFF
            set_mode(fnt_expanded);
            reset_cmd();
            break;
        case 15: // double width OFF
            clear_mode(fnt_expanded);
            reset_cmd();
            break;
        case 17: // Proportional character set ON
            set_mode(fnt_proportional);
            clear_mode(fnt_compressed);
            reset_cmd();
            break;
        case 19: // Select 10 CPI
            clear_mode(fnt_proportional | fnt_compressed);
            reset_cmd();
            break;
        case 20: // Turns on compressed mode.
            set_mode(fnt_compressed);
            clear_mode(fnt_proportional);
            reset_cmd();
            break;
        case 28:                       // half forward line feed
            pdf_dY -= lineHeight / 2.; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            reset_cmd();
            break;
        case 30:                       // half reverse line feed
            pdf_dY += lineHeight / 2.; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            reset_cmd();
            break;
        default:
            reset_cmd();
            break;
        }
    }
    else
    { // check for other commands or printable character
        switch (c)
        {
        case 8:                                                            // Backspace. Empties printer buffer, then backspaces print head one space
            fprintf(_file, ")%d(", (int)(charWidth / lineHeight * 1000.)); // TODO update for 825 font
            pdf_X -= charWidth;                                            // update x position
            break;
        case 10:                  // Line Feed. Printer empties its buffer and does line feed at
                                  // current line spacing and Resets buffer pointer to zero
            pdf_dY -= lineHeight; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            break;
        case 13: // Carriage Return.
            // Prints buffer contents and resets buffer character count to zero
            // Implemented outside in pdf_printer()
            break;
        case 14: // Turns on double width mode to end of line unless cancelled by 20
            clear_mode(fnt_underline);
            break;
        case 15: // Turns on compressed character mode. Does not work with
                 // emphasized mode. Stays on until cancelled by OC2 (18)
            set_mode(fnt_underline);
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
                    float new_w = epson_font_width(epson_font_mask);
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

uint8_t atari825::epson_font_lookup(uint16_t code)
{
    // substitude 1025 fonts for now
    // TODO: change to 825 fonts
    uint16_t c = code & (fnt_expanded | fnt_compressed);
    if (c == 0)
        return 1;
    else if (c & fnt_expanded)
        return 2;
    else
        return 3;
    return 1;
}

float atari825::epson_font_width(uint16_t code)
{
    // substitude 1025 fonts for now
    // TODO: change to 825 fonts
    uint8_t F = epson_font_lookup(code);
    const float w[] = {7.2, 14.4, 4.0};
    return w[F];
}

void atari825::epson_set_font(uint8_t F, float w)
{
    fprintf(_file, ")]TJ /F%u 12 Tf [(", F);
    charWidth = w;
    fontNumber = F;
    fontUsed[F] = true;
}

void atari825::post_new_file()
{
    translate850 = true;
    _eol = ASCII_CR;

    shortname = "a1025"; //  TODO: change to Atari 825 fonts a825

    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 18.0;
    bottomMargin = 0;
    printWidth = 576.0; // 8 inches
    lineHeight = 12.0;
    charWidth = 7.2;
    fontNumber = 1;
    fontSize = 9;
    fontHorizScale = 120;
    textMode = true;
    pdf_dY = 0;

    pdf_header();
    escMode = false;
}
