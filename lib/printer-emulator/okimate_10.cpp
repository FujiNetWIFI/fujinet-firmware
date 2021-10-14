#include "okimate_10.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

/**
 * Okimate 10 state machine
 *
 * The Okimate 10 has both ESC and direct commands
 * Several commands have an additional argument and one has two arguments
 *
 * I use the epson_cmd state machine approach for both direct commands and ESC sequences.
 * There are two flags: cmdMode and escMode. cmdMode is set once a direct command with an argument is called.
 * escMode is set when ESC is received like normal. I chose to use the dot graphics like epson, too. This
 * allows mixed text and graphics in color.
 *
 * The colorMode is very different than anything else. Instead of printing char by char, three complete lines
 * of text and/or graphics will be buffered up as received. CMD and ESC sequences will be executed. For any
 * action that would normally write to the PDF immediately, a special COLOR case will be added to write to
 * an accompanying state array that parallels the buffer arrays. Once all three lines are received (CMY colors),
 * they will be printed in color. This will require a seperate loop to print. The colors will be chosen by
 * comparing the character/graphics bytes across the CMY buffers and setting the color register appropriately.
 * If there's a characeter mismatch between buffers (except for SPACE), then I might just print the two chars
 * in CMY order overlapping. Otherwise, the correct colors will be chosen.
 */

void okimate10::esc_not_implemented()
{
    uint8_t c = okimate_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: ESC %u %02x %c\n", c, c, c);
}

void okimate10::cmd_not_implemented(uint8_t c)
{
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: %u %02x %c\n", c, c, c);
}

void okimate10::set_mode(uint8_t m)
{
    okimate_new_fnt_mask |= m;
}

void okimate10::clear_mode(uint8_t m)
{
    okimate_new_fnt_mask &= ~m;
}

void okimate10::reset_cmd()
{
    escMode = false;
    cmdMode = false;
    okimate_cmd.cmd = 0;
    okimate_cmd.ctr = 0;
    okimate_cmd.n = 0;
    okimate_cmd.data = 0;
}

void okimate10::fprint_color_array(uint8_t font_mask)
{
    for (int i = 0; i < 4; i++)
    {
        fprintf(_file, " %d", (font_mask >> (i + 4) & 0x01));
    }
    fprintf(_file, " k ");
}

void okimate10::okimate_set_char_width()
{
    double w = font_widths[okimate_new_fnt_mask & 0x03];
    charWidth = w * 7.2 / 100.;
}

void okimate10::okimate_handle_font()
{
    /**
 * need new logic. So "truth table"...
 * 
 * need to CHANGE if 
 *      okimate_current_fnt_mask == invalid_font
 *      or
 *      okimate_current_fnt_mask != okimate_new_fnt_mask
 * 
 * If CHANGE==true figure out if its the FONT or COLOR thats different:
 *      change_font and color if okimate_current_fnt_mask == invalid_font
 *      change_font = okimate_current_fnt_mask & 0x0b != okimate_new_fnt_mask 0x0b
 *              note: 0x04 is inverse text so check afterwards
 *      change_color = okimate_current_fnt_mask & 0xf0 != okimate_new_fnt_mask 0xf0
 *                 or when leaving inverse mode assuming current != invalid_font: 
 *                      (okimate_current_fnt_mask & fnt_inverse) && !(okimate_new_fnt_mask & fnt_inverse)
 * 
 * if change_font check if text or graphics
 *      it's gfx if fnt_gfx is set in new font - could also reinforce with new font & 0x07 == 0
 *      otherwise it's text
 * 
 * if change_color just change the color
 * 
 * make the changes
 * 
 * finally check if inverse mode
 * 
 * */
    bool fnt_is_invalid = (okimate_current_fnt_mask == invalid_font);
    bool need_to_change = (okimate_current_fnt_mask != okimate_new_fnt_mask) || fnt_is_invalid;
    bool change_font = fnt_is_invalid || ((okimate_current_fnt_mask & 0x0f) != (okimate_new_fnt_mask & 0x0f));
    bool change_color = fnt_is_invalid || ((okimate_current_fnt_mask & 0xf0) != (okimate_new_fnt_mask & 0xf0));
    change_color |= !fnt_is_invalid && ((okimate_current_fnt_mask & fnt_inverse) && !(okimate_new_fnt_mask & fnt_inverse));
    bool fnt_is_reverse = (okimate_new_fnt_mask & fnt_inverse);

    if (!(need_to_change || fnt_is_reverse))
        return;

    if (!BOLflag)
        fprintf(_file, ")]TJ\n ");

    if (okimate_new_fnt_mask & fnt_gfx)
    {
        if (fnt_is_invalid || !(okimate_current_fnt_mask & fnt_gfx))
        {
            charWidth = 1.2;
            fprintf(_file, "/F2 12 Tf 100 Tz"); // set font to GFX mode
            fontUsed[1] = true;
        }
    }
    else if (change_font) // if text mode changed
    {
        okimate_set_char_width();
        double w = font_widths[okimate_new_fnt_mask & 0x03];
        fprintf(_file, "/F1 12 Tf %g Tz", w);
    }

    // check and change color or reset font color when leaving REVERSE mode
    if (change_color)
    {
        fprint_color_array(okimate_new_fnt_mask);
    }

    okimate_current_fnt_mask = okimate_new_fnt_mask;

    if (fnt_is_reverse)
    {
        // make a rectangle "x y l w re f"
        fprint_color_array(okimate_current_fnt_mask);
        fprintf(_file, "%g %g %g 7 re f 0 0 0 0 k ", pdf_X + leftMargin, pdf_Y, charWidth);
    }

    fprintf(_file, " [(");
}

uint16_t okimate10::okimate_cmd_ascii_to_int(uint8_t c)
{
    // now bytes to int
    uint16_t N = okimate_cmd.n; // - 48;
    if (okimate_cmd.ctr == 1)
        return N;
    N *= 10;
    N += okimate_cmd.data; // - 48;
    if (okimate_cmd.ctr == 2)
        return N;
    N *= 10;
    N += c; // - 48;
    return N;
}

void okimate10::print_7bit_gfx(uint8_t c)
{
    // e.g., [(0)99(1)99(4)99(50)]TJ
    // lead with '0' to enter a space
    // then shift back with 100 and print each pin
    fprintf(_file, "0");
    for (int i = 0; i < 7; i++)
    {
        if ((c >> (6 - i)) & 0x01) // have the gfx font points backwards or Okimate dot-graphics are upside down
            fprintf(_file, ")99(%u", i + 1);
    }
}

void okimate10::pdf_clear_modes()
{
    clear_mode(fnt_inverse); // implied by Atari manual page 28. Explicit in Commod'e manual page 26.
}

void okimate10::okimate_init_colormode()
{
    colorMode = colorMode_t::yellow; // first color in YMC ribbon
    Debug_printf("Align Ribbon. colorMode = %d\n", static_cast<int>(colorMode));
    color_counter = 0;
    // initialize the color content buffer
    for (int i = 0; i < 480; i++) // max 480 dots per line
    {
        color_buffer[i][0] = invalid_font; // clear font
        for (int j = 1; j < 4; j++)
        {
            color_buffer[i][j] = 0; // fill
        }
    }
}

void okimate10::okimate_next_color()
{
    if (colorMode != colorMode_t::off) // are we in color mode?
    {
        // expect 3 EOL's in color mode, one after each color.
        color_counter = 0;
        pdf_X = 0;
        colorMode = static_cast<colorMode_t>(static_cast<int>(colorMode) + 1); // increment colorMode
        Debug_printf("EOL received. colorMode = %d\n", static_cast<int>(colorMode));
        if (colorMode == colorMode_t::process) // if done all three colors, then output
        {
            // output the color buffer and reset the colorMode state var
            Debug_println("Will now output color line.");
            okimate_output_color_line();
            Debug_println("Resetting colorMode to OFF");
            colorMode = colorMode_t::off;
        }
    }
}

void okimate10::okimate_output_color_line()
{
    uint16_t i = 0;
    okimate_current_fnt_mask = invalid_font; //invalidate font
    // Debug_printf("Color buffer element 0: %02x\n", color_buffer[0][0]);
    for (i = 0; i < 480; i++) //while (color_buffer[i][0] != invalid_font && i < 480)
    {
        if (color_buffer[i][0] == invalid_font)
            break;
        if (color_buffer[i][0] != skip_me)
        { // Debug_printf("Color buffer position %d\n", i);
            // in text or gfx mode?
            if (color_buffer[i][0] & fnt_gfx)
            {
                uint8_t c = 0;
                // color dot graphics
                // Debug_printf("color gfx: ctr, char's: %03d %02x %02x %02x\n", i, color_buffer[i][1], color_buffer[i][2], color_buffer[i][3]);
                okimate_new_fnt_mask = 0;
                set_mode(fnt_gfx);
                okimate_handle_font(); // switch to gfx mode in case c = 0
                // brute force coding for colors: okimate prints in Y-M-C order
                // 111 Y&M&C black
                c = color_buffer[i][1] & color_buffer[i][2] & color_buffer[i][3];
                if (c)
                {
                    set_mode(fnt_C | fnt_M | fnt_Y);
                    okimate_handle_font();
                    print_7bit_gfx(c);
                    fprintf(_file, ")99(");
                }
                // 110 Y&M
                c = color_buffer[i][1] & color_buffer[i][2] & ~color_buffer[i][3];
                if (c)
                {
                    set_mode(fnt_Y | fnt_M);
                    clear_mode(fnt_C);
                    okimate_handle_font();
                    print_7bit_gfx(c);
                    fprintf(_file, ")99(");
                }
                // 101 C&Y
                c = color_buffer[i][1] & ~color_buffer[i][2] & color_buffer[i][3];
                if (c)
                {
                    set_mode(fnt_C | fnt_Y);
                    clear_mode(fnt_M);
                    okimate_handle_font();
                    print_7bit_gfx(c);
                    fprintf(_file, ")99(");
                }
                // 110 M&C
                c = ~color_buffer[i][1] & color_buffer[i][2] & color_buffer[i][3];
                if (c)
                {
                    set_mode(fnt_M | fnt_C);
                    clear_mode(fnt_Y);
                    okimate_handle_font();
                    print_7bit_gfx(c);
                    fprintf(_file, ")99(");
                }
                // 100 Y
                c = color_buffer[i][1] & ~color_buffer[i][2] & ~color_buffer[i][3];
                if (c)
                {
                    set_mode(fnt_Y);
                    clear_mode(fnt_C | fnt_M);
                    okimate_handle_font();
                    print_7bit_gfx(c);
                    fprintf(_file, ")99(");
                }
                // 010 M
                c = ~color_buffer[i][1] & color_buffer[i][2] & ~color_buffer[i][3];
                if (c)
                {
                    set_mode(fnt_M);
                    clear_mode(fnt_C | fnt_Y);
                    okimate_handle_font();
                    print_7bit_gfx(c);
                    fprintf(_file, ")99(");
                }
                // 001 C
                c = ~color_buffer[i][1] & ~color_buffer[i][2] & color_buffer[i][3];
                if (c)
                {
                    set_mode(fnt_C);
                    clear_mode(fnt_M | fnt_Y);
                    okimate_handle_font();
                    print_7bit_gfx(c);
                    fprintf(_file, ")99(");
                }
                fprintf(_file, " ");
                pdf_X += charWidth;
            }
            else
            {
                // color text
                uint8_t c = ' ';
                // first, set the font mode and clear color
                okimate_new_fnt_mask = color_buffer[i][0] & 0x07;
                // then figure out color
                // clear_mode(fnt_C | fnt_M | fnt_Y | fnt_K);
                if ((color_buffer[i][1] != 0) && (color_buffer[i][1] != ' '))
                {
                    set_mode(fnt_Y);
                    c = color_buffer[i][1];
                }
                if ((color_buffer[i][2] != 0) && (color_buffer[i][2] != ' '))
                {
                    set_mode(fnt_M);
                    c = color_buffer[i][2];
                }
                if ((color_buffer[i][3] != 0) && (color_buffer[i][3] != ' '))
                {
                    set_mode(fnt_C);
                    c = color_buffer[i][3];
                }
                Debug_printf("color text: ctr, font, char: %03d %02x %02x\n", i, okimate_new_fnt_mask, c);
                // handle fnt
                okimate_handle_font();
                // output character
                print_char(c);
            }
            //i++;
        }
    }
    //okimate_current_fnt_mask = 0xFF;
    okimate_new_fnt_mask = 0x80; // set color back to
#ifdef DEBUG
    Debug_println("Color output line complete");
#endif
    fprintf(_file, ")]TJ\n"); // close the line
    pdf_X = 0;                // CR
    pdf_clear_modes();
    fprintf(_file, "0 0 Td [(");
    BOLflag = false;
    //pdf_end_line();
    //pdf_new_line();
}

void okimate10::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
{
    // Okimate 10 extras codes:
    // ESC CTRL-T ESC CTRL-N - 8.25 char/inch (0x14, 0x0E)
    // 0x99     Align Ribbon (for color mode)
    // 0x9B     EOL for color mode

    // 0x8A n    n/144" line advance (n * 1/2 pt vertial line feed)
    // 0x8C     form feed
    // ESC A - perforation skip OFF
    // ESC B - perforation skip ON

    // 146 0x92 - start REVERSE mode
    // 147 0x93 - stop REVERSE mode

    // ESC '%' - start graphics mode
    // 0x91 - stop graphics mode
    // 0x9A n data - repeat graphics data n times

    // 0x90 n - dot column horizontal tab

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

        // command state machine switching
        if (okimate_cmd.cmd == 0)
        {
            okimate_cmd.ctr = 0;
            okimate_cmd.cmd = c; // assign command char
#ifdef DEBUG
            Debug_printf("Command: %02x\n", c);
#endif
        }
        else
        {
            okimate_cmd.ctr++; // increment counter to keep track of the byte in the command
#ifdef DEBUG
            Debug_printf("Command counter: %d\n", okimate_cmd.ctr);
#endif
        }

        if (okimate_cmd.ctr == 1)
        {
            okimate_cmd.n = c;
#ifdef DEBUG
            Debug_printf("n: %d\n", c);
#endif
        }
        else if (okimate_cmd.ctr == 2)
        {
            okimate_cmd.data = c;
#ifdef DEBUG
            Debug_printf("data: %d\n", c);
#endif
        }

        // state machine actions
        switch (okimate_cmd.cmd)
        {
        case 0x0E: // wide ESC & 14
            // change font to elongated like
            set_mode(fnt_expanded);
            reset_cmd();
            break;
        case 0x0F: // normal ESC & 15
            // change font to normal
            clear_mode(fnt_expanded | fnt_compressed);
            reset_cmd();
            break;
        case 0x14: // fine ESC & 20
            // change font to compressed
            clear_mode(fnt_expanded); // remove wide print mode - shown in Commod'e manual
            set_mode(fnt_compressed);
            reset_cmd();
            break;
        case 0x17: // 23
            intlFlag = true;
            reset_cmd();
            break;
        case 0x18: // 24
            intlFlag = false;
            reset_cmd();
            break;
        case 0x25: // 37, '%': // GRAPHICS MODE ON
            if (okimate_cmd.ctr == 0)
            {
                okimate_old_fnt_mask = okimate_new_fnt_mask; //okimate_current_fnt_mask;
                set_mode(fnt_gfx);
                clear_mode(fnt_compressed | fnt_inverse | fnt_expanded); // may not be necessary
                // charWidth = 1.2;
                // fprintf(_file, ")]TJ /F2 12 Tf 100 Tz [("); // set font to GFX mode
                // fontUsed[1] = true;
                // do I need to write out new font now? How to handle switchting to color mode after gfx?
                // need to catch 0x99 while in 0x25 esc mode!
                if (colorMode == colorMode_t::off)
                    okimate_handle_font();
                else
                    charWidth = 1.2;
                //    okimate_current_fnt_mask = okimate_new_fnt_mask;
                textMode = false;
#ifdef DEBUG
                Debug_printf("Entering GFX mode\n");
#endif
            }
            else
                switch (c)
                {
                case 0x8A: // line advance by n
                    // logic needed:
                    // set cmdMode and clear escMode
                    // pass control to 0x8A mode
                    // let it execute a line advance
                    // get control back
                    escMode = false;
                    cmdMode = true;
                    okimate_cmd.cmd = 0x8A;
                    okimate_cmd.ctr = 0;
#ifdef DEBUG
                    Debug_printf("Go to line advance from gfx\n");
#endif
                    break;
                case 0x91: // end gfx mode
                    // reset font
                    okimate_current_fnt_mask = invalid_font;     // invalidate font mask
                    okimate_new_fnt_mask = okimate_old_fnt_mask; // restore old font
                    if (colorMode == colorMode_t::off)
                        okimate_handle_font();
                    else
                        okimate_set_char_width();
                    //    okimate_current_fnt_mask = okimate_new_fnt_mask;
                    textMode = true;
                    reset_cmd();
#ifdef DEBUG
                    Debug_printf("Finished GFX mode\n");
#endif
                    break;
                case 0x99: // 0x99     Align Ribbon (for color mode)
                    okimate_init_colormode();
                    break;
                case 0x9A: // repeat gfx char n times
                    // toss control over to the Direct Command switch statement
                    escMode = false;
                    cmdMode = true;
                    okimate_cmd.cmd = 0x9A;
                    okimate_cmd.ctr = 0;
#ifdef DEBUG
                    Debug_printf("Go to repeated gfx char\n");
#endif
                    break;
                case 0x9B:
                    okimate_next_color();
                    break;
                default:
                    if (colorMode == colorMode_t::off)
                        print_7bit_gfx(c);
                    else
                    {
                        if ((color_buffer[color_counter][0] & fnt_gfx) || (color_buffer[color_counter][1] == 0x20) || (color_buffer[color_counter][2] == 0x20)) // either gfx or invalid but not skip_me) // either gfx or invalid but not skip_me
                        {
                            color_buffer[color_counter][0] = fnt_gfx; // just need font/gfx state - not color
                            color_buffer[color_counter][static_cast<int>(colorMode)] = c;
                        }
                        if (color_counter < 479)
                        {
                            color_counter++;
                            pdf_X += charWidth;
                        }
                    }

                    break;
                }
            break;
        case 0x36:             // '6'
            lineHeight = 12.0; //72.0/6.0;
            reset_cmd();
            break;
        case 0x38:            // '8'
            lineHeight = 9.0; //72.0/8.0;
            reset_cmd();
            break;
        case 0x41: // PERFORATION SKIP OFF
            bottomMargin = 0.0;
            topMargin = 0.0;
            reset_cmd();
            break;
        case 0x42: // PERFORATION SKIP ON
            bottomMargin = 72.0;
            topMargin = 72.0;
            reset_cmd();
            break;
        case 0x4c: // 'L'
            if (colorMode == colorMode_t::off)
                set_line_long();
            else
                cmd_not_implemented(c);
            reset_cmd();
            break;
        case 0x53: // 'S'
            if (colorMode == colorMode_t::off)
                set_line_short();
            else
                cmd_not_implemented(c);
            reset_cmd();
            break;
        default:
            reset_cmd();
            break;
        }
    }
    else if (cmdMode)
    {
        // command state machine switching
        if (okimate_cmd.ctr == 0)
        {
#ifdef DEBUG
            Debug_printf("Command: %02x\n", okimate_cmd.cmd);
#endif
        }

        okimate_cmd.ctr++; // increment counter to keep track of the byte in the command
#ifdef DEBUG
        Debug_printf("Command counter: %d\n", okimate_cmd.ctr);
#endif

        if (okimate_cmd.ctr == 1)
        {
            okimate_cmd.n = c;
#ifdef DEBUG
            Debug_printf("n: %d\n", c);
#endif
        }
        else if (okimate_cmd.ctr == 2)
        {
            okimate_cmd.data = c;
#ifdef DEBUG
            Debug_printf("data: %d\n", c);
#endif
        }

        switch (okimate_cmd.cmd)
        {
        case 0x8A:
            // n/144" line advance (n * 1/2 pt vertial line feed)
            // D:LEARN demo sends 0x8A in middle of color mode
            // behavior looks like it continues on the next line
            // so in emulator, need to spit out current buffer, clear it, and continue in the state machine
            if (colorMode != colorMode_t::off)
            {
                colorMode_t temp = colorMode;
                okimate_output_color_line();
                okimate_init_colormode(); // this resets color mode to yellow
                colorMode = temp;         // pick back up where we left off
            }
            // {
            pdf_dY -= double(okimate_cmd.n) * 72. / 144. - lineHeight; // set pdf_dY and rise to fraction of line
            pdf_set_rise();
            pdf_end_line(); // execute a CR and custom line feed
            pdf_new_line();
            // }
            // else
            //     cmd_not_implemented(0x8A);
            if (textMode)
                reset_cmd();
            else
            {
                // pass control back to graphics mode
                cmdMode = false;
                escMode = true;
                okimate_cmd.cmd = 37; // graphics
                okimate_cmd.ctr = 1;
            }
            break;
        case 0x90: //0x90 n - dot column horizontal tab
            textMode = false;
            if ((c > 9) || (okimate_cmd.ctr == 3)) // i think should always be ctr==3
            {
                uint16_t N = okimate_cmd_ascii_to_int(c);
                if (N > 480)
                {
                    N = 0;
                    Debug_printf("!!!!Tab Value out of Range: %d", N);
                }
                /**
                 * new logic - go to position N and start printing from there
                 * recipe
                 *      pdf_X stores current position
                 *      assume can only tab to right - N(in pts)>pdf_X
                 *      N converts to pts by 60 dots per inch = 72 pts per inch : 72/60 = 1.2
                 *      need to insert N-pdf_X/1.2 dots from current position
                */
                okimate_old_fnt_mask = okimate_new_fnt_mask;
                set_mode(fnt_gfx);
                clear_mode(fnt_compressed | fnt_inverse | fnt_expanded); // may not be necessary
                if (colorMode == colorMode_t::off)
                    okimate_handle_font();
                else
                    okimate_set_char_width();
                //    okimate_current_fnt_mask = okimate_new_fnt_mask;

                // compute gap needed in dots
                if (colorMode == colorMode_t::off)
                {
                    uint8_t M = N - uint8_t(pdf_X / 1.2);
                    for (int i = 1; i < M; i++) // i=1 for BW on D:LEARN
                    {
                        fprintf(_file, " ");
                        pdf_X += charWidth;
                    }
                }
                else
                {
                    uint8_t M = N - color_counter;
                    for (int i = 0; i < M; i++) // i=0 for COLOR on D:LEARN
                    // if in color mode, store a ' ' in the buffer
                    {
                        if (color_buffer[color_counter][0] & fnt_gfx) // either gfx or invalid but not skip_me
                        {
                            color_buffer[color_counter][0] = fnt_gfx;                     // okimate_current_fnt_mask & 0x0f; // just need font/gfx state - not color
                            color_buffer[color_counter][static_cast<int>(colorMode)] = 0; // space no dots
                        }
                        if (color_counter < 479)
                        {
                            color_counter++;
                            pdf_X += charWidth;
                        }
                    }
                }
                okimate_new_fnt_mask = okimate_old_fnt_mask; // set back to old state
                // old statement: okimate_new_fnt_mask = okimate_current_fnt_mask!; // this doesn't do anything because of next line
                okimate_current_fnt_mask = invalid_font; // invalidate font mask
                if (colorMode == colorMode_t::off)
                    okimate_handle_font();
                else
                    okimate_set_char_width();
                //    okimate_current_fnt_mask = okimate_new_fnt_mask;
                textMode = true;
                reset_cmd();
            }
            break;
        case 0x9A: // 0x9A n data - repeat graphics data n times
            // receive control from the ESC-37 graphics mode
            if (okimate_cmd.ctr > 1)
            {
                for (int i = 0; i < okimate_cmd.n; i++)
                {
                    if (colorMode == colorMode_t::off)
                    {
                        print_7bit_gfx(okimate_cmd.data);
                        pdf_X += charWidth;
                    }
                    else
                    {
                        if ((color_buffer[color_counter][0] & fnt_gfx) || (color_buffer[color_counter][static_cast<int>(colorMode)] == 0x20)) // either gfx or invalid but not skip_me
                        {
                            color_buffer[color_counter][0] = fnt_gfx; // just need font/gfx state - not color
                            color_buffer[color_counter][static_cast<int>(colorMode)] = okimate_cmd.data;
                        }
                        if (color_counter < 479)
                        {
                            color_counter++;
                            pdf_X += charWidth;
                        }
                    }
                }
                // toss control back over to ESC-37 graphics mode
                cmdMode = false;
                escMode = true;
                okimate_cmd.cmd = 37; // graphics
                okimate_cmd.ctr = 1;
            }
            break;
        default:
            esc_not_implemented();
            reset_cmd();
            break;
        }
    }
    else
    {
        // Okimate 10 extras codes:
        // 0x99     Align Ribbon (for color mode)
        // 0x9B     EOL for color mode

        // 0x8A n    n/144" line advance (n * 1/2 pt vertial line feed)
        // 0x8C     form feed

        // 146, 0x92 - start REVERSE mode
        // 147, 0x93 - stop REVERSE mode

        // 0x91 - stop graphics mode
        // 0x9A n data - repeat graphics data n times

        // 0x90 n - dot column horizontal tab

        switch (c)
        {
        case 27:
            reset_cmd(); // clear command record to set up for fresh esc sequence
            escMode = true;
            break;
        case 0x8A: // n/144" line advance (n * 1/2 pt vertial line feed)
            cmdMode = true;
            okimate_cmd.cmd = c;
            okimate_cmd.ctr = 0;
            break;
        case 0x8C: // formfeed!
            if (colorMode != colorMode_t::off)
            {
                colorMode_t temp = colorMode;
                okimate_output_color_line();
                okimate_init_colormode(); // this resets color mode to yellow
                colorMode = temp;         // pick back up where we left off
            }
            pdf_end_page();
            pdf_new_page();
            pdf_new_line();
            break;
        case 0x90: // 0x90 n - dot column horizontal tab
            cmdMode = true;
            okimate_cmd.cmd = c;
            okimate_cmd.ctr = 0;
            break;
        case 0x92:                 // start REVERSE mode
            set_mode(fnt_inverse); // see clear_modes(), reverse clears at EOL
            break;
        case 0x93: // stop REVERSE mode
            clear_mode(fnt_inverse);
            break;
        case 0x99: // 0x99     Align Ribbon (for color mode)
            okimate_init_colormode();
            break;
        case 0x9A: // 0x9A n data - repeat graphics data n times
            cmdMode = true;
            okimate_cmd.cmd = c;
            okimate_cmd.ctr = 0;
            break;
        case 0x9B: // 0x9B     EOL for color mode
            okimate_next_color();
            break;
        default:
            if (colorMode == colorMode_t::off)
            {
                okimate_handle_font();
                print_char(c);
            }
            else
            {
                // if space, need to check if gfx already there, otherwise print it
                okimate_set_char_width();
                if ((c == 0x20) && (color_buffer[color_counter][0] == fnt_gfx))
                    color_buffer[color_counter][static_cast<int>(colorMode)] = 0;
                else
                {
                    color_buffer[color_counter][0] = okimate_new_fnt_mask & 0x07; // just need font state - not color
                    color_buffer[color_counter][static_cast<int>(colorMode)] = c;
                }
                int ndots = (int)(charWidth * 10. + 1.) / 12 - 1; // number-1 of 1.2pt dots in charWidth
                for (int i = 0; i < ndots; i++)
                {
                    if (color_counter < 479)
                        color_counter++;
                    if ((c == 0x20) && (color_buffer[color_counter][0] == fnt_gfx))
                        color_buffer[color_counter][static_cast<int>(colorMode)] = 0;
                    else
                    {
                        color_buffer[color_counter][0] = skip_me;
                        color_buffer[color_counter][static_cast<int>(colorMode)] = c;
                    }
                }
                if (color_counter < 479)
                    color_counter++;
                pdf_X += charWidth;
            }
            break;
        }
    }
}

void okimate10::post_new_file()
{
    atari1025::post_new_file();
    shortname = "oki10";
    topMargin = 72.0 / 2.0 - 1.0; // perf skip is default with 1/2 inch margins
    //pdf_dY = topMargin;       // but start at top of first page
    bottomMargin = topMargin; // perf skip is default
}
