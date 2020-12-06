#include "atari_xdm121.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

uint8_t xdm121::xdm_font_lookup(uint16_t code)
{
    if (code & fnt_doublestrike)
        return 2;
    return 1;
}

double xdm121::xdm_font_width(uint16_t code)
{
    if (code & fnt_elite)
        return 6.;
    return 7.2;
}

void xdm121::xdm_set_font(uint8_t F, double w)
{
    double p = (charPitch - w) / w;
    fprintf(_file, ")]TJ /F%u 12 Tf %g Tc [(", F, p);
    charWidth = w;
    fontNumber = F;
    fontUsed[F - 1] = true;
}

void xdm121::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
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
            // if (epson_font_mask & fnt_proportional)
            // {
            //     fprintf(_file, " )%d(", (int)(280 - epson_cmd.cmd * 40));
            //     pdf_X += 0.48 * (double)epson_cmd.cmd;
            // }
        case 9: // XDM absolute horizontal tab
            if (epson_cmd.ctr > 0)
            {
                esc_not_implemented();
                reset_cmd();
            }
            break;
        case 10:                  // XDM full reverse line feed
            pdf_dY += lineHeight; // set pdf_dY and rise to N1/216.
            pdf_set_rise();
            reset_cmd();
            break;
        case 23: // XDM // international ON
            intlFlag = true;
            reset_cmd();
            break;
        case 24: // XDM // international OFF
            intlFlag = false;
            reset_cmd();
            break;
        case 25: // XDM // underline on
            set_mode(fnt_underline);
            reset_cmd();
            break;
        case 26: // XDM // underline off
            clear_mode(fnt_underline);
            reset_cmd();
            break;
        case 28: // XDM  half forward line feed (sub script)
            pdf_dY -= (lineHeight / 2.);
            pdf_set_rise();
            reset_cmd();
            break;
        case 29: // XDM // Sets line spacing to N/48". Stays on until changed
            if (epson_cmd.ctr > 0)
            {
                lineHeight = 72.0 * (double)epson_cmd.N1 / 48.0;
                reset_cmd();
            }
            break;
        case 30: // XDM  half reverse line feed (super script)
            pdf_dY += (lineHeight / 2.);
            pdf_set_rise();
            reset_cmd();
            break;
        case 31: // XDM
            // esc_not_implemented();
            // TODO set print pitch and adjust spacing (wonder if can use PDF command to do this or need to adjust each char)
            if (epson_cmd.ctr > 0)
            {
                // TODO: set width flag that can be used in find width function?
                charPitch = ((double)epson_cmd.N1 - 1.) * 72. / 120.;
                reset_cmd();
            }
            break;
        case '#': // XDM 35 - set Horizontal Tab
            esc_not_implemented();
            break;
        case '$': // XDM 36 - clear horiztonal tab
            esc_not_implemented();
            break;
        case 39: // XDM clear all tab stops
            esc_not_implemented();
            break;
        case '(': // XDM 40 - set left margin
            esc_not_implemented();
            break;
        case '0': // XDM - turn off paper-out sensor
        case '/': // XDM - turn on paper-out sensor
            esc_not_implemented();
            reset_cmd();
            break;
        case '@': // XDM // Resets all special modes to power up state including Top Of Form
                  // need to reset font to normal
                  // not sure what to do about TOF?
#ifdef DEBUG
            Debug_printf("@ reset!\n");
#endif
            at_reset();
            xdm_set_font(xdm_font_lookup(0), 7.2);
            reset_cmd();
            break;
        case 'C': // XDM
            // Sets form length (FL) to N lines. Default is 66
            // Format: <ESC>"C" N, 1 < = N < = 127.
            // Resets top of form.
            esc_not_implemented();
            if (epson_cmd.ctr > 0)
                reset_cmd();
            break;
        case 'E': // XDM // Turns on "bold" mode
            set_mode(fnt_doublestrike);
            reset_cmd();
            break;
        case 'F': // XDM // Turns off "bold" mode
            clear_mode(fnt_doublestrike);
            reset_cmd();
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
        case 8: // XDM Backspace. Empties printer buffer, then backspaces print head one space
            if (intlFlag)
                break;
            /*MX Printer with GRAFTRAXplus Manual page 6-3:
            One quirk in using the backspace. In expanded mode, CHR$(8) causes a full double
            width backspace as we would expect. The fun begins when several backspaces
            are done in succession. All except for the first one are normal-width backspaces */
            fprintf(_file, ")%d(", (int)(charWidth / lineHeight * 1000.));
            pdf_X -= charWidth; // update x position
            // XMM
            break;
        case 9: // XDM Horizontal Tabulation. Print head moves to next tab stop
            if (intlFlag)
                break;
            not_implemented();
            // XMM
            break;
        case 10: // XDM Line Feed. Printer empties its buffer and does line feed at
            if (intlFlag)
                break;
            // current line spacing and Resets buffer pointer to zero
            pdf_dY -= lineHeight; // set pdf_dY and rise to one line
            pdf_set_rise();
            // XMM
            break;
        case 12: // XDM Advances paper to next logical TOF (top of form)
            if (intlFlag)
                break;
            pdf_end_page();
            // XMM
            break;
        case 13: // XDM Carriage Return.
            if (intlFlag)
                break;
            // does a real CR w/out LF
            // XDM
            not_implemented();
            break;
        case 14: // XDM Turns off underline
            if (intlFlag)
                break;
            clear_mode(fnt_underline);
            // XMM
            break;
        case 15: // XDM Turns on underline
            if (intlFlag)
                break;
            set_mode(fnt_underline);
            // XMM
            break;
        case 27: // ESC mode
            escMode = true;
            break;
        default: // maybe printable character
                 // TODO: need INTERNATIONAL CHARACTERS

            // adjust typeface font
            uint8_t new_F = xdm_font_lookup(epson_font_mask);
            if (fontNumber != new_F)
            {
                double new_w = xdm_font_width(epson_font_mask);
                xdm_set_font(new_F, new_w);
            }

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
                {
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
                }
                if (valid)
                {
                    fputc(d, _file);
                    if (epson_font_mask & fnt_underline)
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

                if (epson_font_mask & fnt_underline)
                    fprintf(_file, ")600(_"); // close text string, backspace, start new text string, write _

                pdf_X += charWidth; // update x position
            }
        }
    }
}


void xdm121::post_new_file()
{
    epson80::post_new_file();
    // TODO: shortname = "xdm121";
    // TODO: can also have 10 pt courier so set a flag to make 10pt?
    // ahah! make our own xdm_set_font() me thinks - this will help with char spacing too
    translate850 = false;
    _eol = ATASCII_EOL;
}
