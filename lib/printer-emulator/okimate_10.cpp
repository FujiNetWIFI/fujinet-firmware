#include "okimate_10.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

/**
 * Okimate 10 state machine
 * 
 * The Okimate 10 has both ESC and direct commands
 * Several commands have an additional argument and one has two arguments 
 * 
 */
void okimate10::esc_not_implemented()
{
    uint8_t c = okimate_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: ESC %u %x %c\n", c, c, c);
}

void okimate10::cmd_not_implemented(uint8_t c)
{
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: %u %x %c\n", c, c, c);
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

void okimate10::okimate_update_font()
{
    if (okimate_current_fnt_mask != okimate_new_fnt_mask)
    {
        fprintf(_file, ")]TJ\n ");
        // check and change typeface
        if ((okimate_current_fnt_mask & 0x03) != (okimate_new_fnt_mask & 0x03))
            switch (okimate_new_fnt_mask & 0x03)
            {
            case 0: // normal
                fprintf(_file, "100 Tz");
                charWidth = 7.2; //72.0 / 10.0;
                break;
            case 1: // fine
                fprintf(_file, "60.606 Tz");
                charWidth = 72.0 / 16.5;
                break;
            case 2: // wide
                fprintf(_file, "200 Tz");
                charWidth = 14.4; //72.0 / 5.0;
                break;
            case 3: // bold
                fprintf(_file, "121.21 Tz");
                charWidth = 72.0 / 8.25;
                break;
            default:
                fprintf(_file, "100 Tz");
                charWidth = 7.2; //72.0 / 10.0;
                break;
            }

        // check and change color
        if ((okimate_current_fnt_mask & 0xF4) != (okimate_new_fnt_mask & 0xF4))
        {
            if (okimate_new_fnt_mask & fnt_inverse)
                fprintf(_file, " 0 0 0 0 k");
            else
                fprintf(_file, " 0 0 0 1 k");
        }
        fprintf(_file, " [(");
        okimate_current_fnt_mask = okimate_new_fnt_mask;
    }
}

void okimate10::print_7bit_gfx(uint8_t c)
{
    // e.g., [(0)100(1)100(4)100(50)]TJ
    // lead with '0' to enter a space
    // then shift back with 133 and print each pin
    fprintf(_file, "0");
    for (int i = 0; i < 7; i++)
    {
        if ((c >> i) & 0x01)
            fprintf(_file, ")133(%u", i + 1);
    }
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
            Debug_printf("Command: %c\n", c);
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
        case 37: // GRAPHICS MODE ON
            esc_not_implemented();
            reset_cmd();
            // TODO: switch font to dot graphics
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
            esc_not_implemented();
            reset_cmd();
            break;
        case 0x42: // PERFORATION SKIP ON
            esc_not_implemented();
            reset_cmd();
            break;
        case 0x4c: // 'L'
            set_line_long();
            reset_cmd();
            break;
        case 0x53: // 'S'
            set_line_short();
            reset_cmd();
            break;
        default:
            reset_cmd();
            break;
        }
        escMode = false;
    }
    else if (cmdMode)
    {
        okimate_cmd.ctr++;
        switch (okimate_cmd.cmd)
        {
        case 0x8A: // n/144" line advance (n * 1/2 pt vertial line feed)
            /* code */
            esc_not_implemented();
            reset_cmd();
            break;
        case 0x90: //0x90 n - dot column horizontal tab
            esc_not_implemented();
            reset_cmd();
            break;
        case 0x9A: // 0x9A n data - repeat graphics data n times
            if (okimate_cmd.ctr > 1)
            {
                esc_not_implemented();
                reset_cmd();
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
            escMode = true;
            break;
        case 0x8A: // n/144" line advance (n * 1/2 pt vertial line feed)
            cmdMode = true;
            okimate_cmd.cmd = c;
            break;
        case 0x8C: // formfeed!
            cmd_not_implemented(c);
            break;
        case 0x90: // 0x90 n - dot column horizontal tab
            cmdMode = true;
            okimate_cmd.cmd = c;
            break;
        case 0x91: // not needed - implement in graphics handling in ESC mode state
            // stop graphics mode
            cmd_not_implemented(c);
            break;
        case 0x92: // start REVERSE mode
            set_mode(fnt_inverse);
            cmd_not_implemented(c);
            break;
        case 0x93: // stop REVERSE mode
            clear_mode(fnt_inverse);
            cmd_not_implemented(c);
            break;
        case 0x99: // 0x99     Align Ribbon (for color mode)
            cmd_not_implemented(c);
            colorMode = true;
            break;
        case 0x9A: // 0x9A n data - repeat graphics data n times
            cmdMode = true;
            okimate_cmd.cmd = c;
            break;
        case 0x9B: // 0x9B     EOL for color mode
            cmd_not_implemented(c);
            break;
        default:
            okimate_update_font();
            print_char(c);
            break;
        }
    }
}

void okimate10::post_new_file()
{
    atari1025::post_new_file();
    shortname = "oki10";
}
