#include "commodoremps803.h"
#include "../../include/debug.h"

void commodoremps803::post_new_file()
{
    shortname = "mps803";
    _eol = ASCII_CR;
    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 18.0;
    bottomMargin = 0;
    printWidth = 576.0; // 8 inches
    lineHeight = 12.0;
    charWidth = 7.2;
    fontNumber = 1;
    fontSize = 12;
    pdf_dY = lineHeight;

    pdf_header();
}

void commodoremps803::pdf_clear_modes()
{
        // need some state variables
    //      enhanced mode
    //      reverse mode
    //      bitmap graphics mode
    //      local char set switch mode - see page 34 or chr$(17) and chr$(145)
    mps_modes.enhanced = false;
    mps_modes.reverse = false;
    mps_modes.bitmap = false;
    mps_modes.local_char_set = false;     
}

void commodoremps803::mps_set_font(uint8_t F)
{
    fprintf(_file, ")]TJ /F%u 12 Tf 100 Tz [(", F);
    switch (F)
    {
    case 1:
    case 3:
        charWidth = 7.2;
        break;
    case 2:
    case 4:
        charWidth = 7.2 * 2.;
        break;
    case 5:
        charWidth = 1.2;
        break;
    default:
        charWidth = 7.2;
        break;
    }
    fontNumber = F;
    fontUsed[F - 1] = true;
}

void commodoremps803::mps_update_font()
{
    uint8_t oldFont = fontNumber;
    
    if (mps_modes.bitmap)
        fontNumber = 5;
    else if (mps_modes.business_char_set)
        fontNumber = 3 + (uint8_t)mps_modes.enhanced;
    else
        fontNumber = 1 + (uint8_t)mps_modes.enhanced;

    if (oldFont != fontNumber)
        mps_set_font(fontNumber);
}

void commodoremps803::reset_cmd()
{
    ctrlMode = false;
    mps_cmd.N[0] = mps_cmd.N[1] = mps_cmd.N[2] = 0;
    mps_cmd.cmd = 0;
    mps_cmd.ctr = 0;
}

void commodoremps803::mps_print_bitmap(uint8_t c)
{
    // e.g., [(0)100(1)100(4)100(50)]TJ
    // lead with '0' to enter a space
    // then shift back with 100 and print each pin
    fprintf(_file, " ");
    for (unsigned i = 0; i < 8; i++)
    {
        if ((c >> i) & 0x01)
            fprintf(_file, ")100(%u", i + 1);
    }
}

void commodoremps803::pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2)
{
    // watch aux1 to change font from 1/2 to 3/4
    // need some state variables
    //      enhanced mode
    //      reverse mode
    //      bitmap graphics mode
    //      local char set switch mode - see page 34 or chr$(17) and chr$(145)
    // there's also case for repeated bit image printing so need ESC-mode type processing

/**
TABLE 1 Page 26 of Users Manual
Special Control Character Summary

Printer function        CHR$ Code                
----------------        ---------
Enhance ON              CHR$(14)
Enhance OFF             CHR$(15)
Bit Image Printing      CHR$(8)
Reverse ON              CHR$(18)
Reverse OFF             CHR$(146)
Carriage Return         CHR$(13)
Line Feed               CHR$(10)
Print in
    Business Mode       CHR$(17)
Print in
    Graphic Mode        CHR$(145) 
Quote                   CHR$(34)
Tab Setting
    the Print Head      CHR$(16); "nHnL"
Repeat Graphic
    Selected            CHR$(8);CHR$(26);CHR$(n);CHR$(Bit Image Data) from page 37
Specify Dot Address     CHRS(27);CHR$(16);CHR$(nH)CHR$(nL)

*/

// font numbers:
// 1 - graphic regular (10 CPI)
// 2 - graphic enhanced (doublewide - 5 CPI)
// 3 - business regular (10 CPI)
// 4 - business enhanced (doublwide - 5 CPI)
// 5 - bit graphics

// determine comm's channel to set font family - "graphics" or "business"
// using special char's 17 or 145 then CR clears back to "global" aux1 setting
    if (!mps_modes.local_char_set)
    {
        mps_modes.business_char_set = (aux1 == 7);
    }

    // process the incoming character
    if (mps_modes.bitmap)
    {
        /* page 27 in user's manual -  enhanced (doublewide) mode:
        CHRS114) and CHRS{15) cancel later mentioned bit image graphic printing code
        [CHRS(8|]. And these control codes give a 1/6 inch line feed.
        */
        switch (c)
        {
        case 14:
            // Enhance ON              CHR$(14)
            mps_modes.enhanced = true;
            mps_modes.bitmap = false;
            lineHeight = 12; // 6 LPI
            break;
        case 15:
            // Enhance OFF             CHR$(15)
            mps_modes.enhanced = false;
            mps_modes.bitmap = false;
            lineHeight = 12; // 6 LPI
            break;
        case 26:
            // repeated bitmap mode
            mps_modes.bitmap = false;
            ctrlMode = true;
            mps_cmd.cmd = 26;
            break;
        default:
            // update font if needed
            mps_update_font();
            mps_print_bitmap(c);
            break;
        }
    }
    else if (ctrlMode)
    {
        mps_cmd.ctr++; // increment counter to keep track of the byte in the command
        Debug_printf("\nCommand counter: %d", mps_cmd.ctr);
        mps_cmd.N[mps_cmd.ctr - 1] = c;
        Debug_printf("\nN[%d]: %d", mps_cmd.ctr, c);
        
        if (mps_cmd.ctr > 1)
            switch (mps_cmd.cmd)
            {
            case 16:
                // Tab Setting
                //     the Print Head      CHR$(16); "nHnL"
                {
                    int n = atoi((char*)mps_cmd.N); // N should be null terminated by reset_cmd()
                    int col = pdf_X/7.2;
                    if (n>col)
                    {
                        if (fontNumber != 1)
                            mps_set_font(1);
                        for (int i = 0; i < n - col; i++)
                            fputc(' ', _file);
                        if (fontNumber != 1)
                            mps_set_font(fontNumber);
                    }
                    reset_cmd();
                } 
                
                break;

            /* page 37 - repeated bit image
            must be in 0x08 mode first
            CHR$(26);CHR$(n);CHR$(Bit Image Data)
            This codes sequence specifies the repeated printing of bit image data, "n" is a
            binary number (0 through 255) which specifies the desired number of the
            printed repetition; followed by one-byte bit image data to be printing repeatedly.
            When 0 is specified for "n", it is read as 256. In order to repeat more than 256
            times the operator needs to use this code twice.
            */
            case 26:
            // FIX TODO - 26 must be called after 08, but 08 puts us into bitmap mode above.
                // Repeat Graphic
                // CHRS(26);CHR$(n);CHR$(Bit Image Data) from page 37
                mps_modes.bitmap = true;
                mps_update_font();
                {
                    int n = mps_cmd.N[0];
                    if (n == 0)
                        n = 256;
                    for (int i = 0; i < n; i++)
                        mps_print_bitmap(c);
                }
                mps_modes.bitmap = false;
                mps_update_font();
                reset_cmd();
                break;
            case 27:
                // Specify Dot Address     CHRS(27);CHR$(16);CHR$(nH)CHR$(nL)
                if (mps_cmd.ctr == 3)
                {
                    int n = (((int)mps_cmd.N[1]) << 8) + (int)mps_cmd.N[2];
                    int col = pdf_X/1.2;
                    if (n>col)
                    {
                        mps_set_font(5);
                        for (int i = 0; i < n - col; i++)
                            fputc(' ', _file);
                        mps_set_font(fontNumber);
                    }
                    reset_cmd();
                } 
                break;
            default:
                Debug_printf("\n BAD PRINTER COMMAND");
                break;
            }
    }
    else
    switch (c)
    {
    /* page 27 in user's manual -  enhanced (doublewide) mode:
    An ASCII CHRSI15) character cancels the character enhancement specified by
    preceding CHR3{14) character or ASCII CHRS(15) is Standard Character Mode.
    */
    case 14:
        // Enhance ON              CHR$(14)
        // TODO cancel graphics
        mps_modes.enhanced = true;
        break; 
    case 15:
        // Enhance OFF             CHR$(15)
        mps_modes.enhanced = false;
        break;
    case 8:
        // Bit Image Printing      CHR$(8)
        mps_modes.bitmap = true;
        // mps_modes.enhanced = false;
        lineHeight = 8; // 9 LPI
        break;
    case 18:
        // Reverse ON              CHR$(18)
        mps_modes.reverse = true;
        break;
    case 146:
        // Reverse OFF             CHR$(146)
        mps_modes.reverse = false;
        break;
    case 10:
        // Line Feed               CHR$(10)
        // DO A CR without reseting modes:
        fprintf(_file, ")]TJ\r\n"); // close the line
        pdf_X = 0; // CR
        BOLflag = true;
        pdf_new_line();
        break;
        
    /* page 34 - switching graphic/business char sets inline
    CHRSC17} functions until CHR$(145) or carriage return is detected.
    CHRS(145) functions until CHRSU7] or carriage return is detected.
    */
    case 17:
        // Print in Business Mode       CHR$(17)
        mps_modes.local_char_set = true;
        mps_modes.business_char_set = true;
        break;
    case 145:
        // Print in Graphic Mode        CHR$(145) 
        mps_modes.local_char_set = true;
        mps_modes.business_char_set = false;
        break;
    case 16:
    // case 26: // should not need case 26 because now handled in bitmap routine
    case 27:
        ctrlMode = true;
        mps_cmd.cmd = c; // assign command char
        Debug_printf("\nCommand: %c", c);
        break;
    default:
        // maybe printable character
        if ((c >= 0x20 && c <= 0x7f) || (c >= 0xa0 && c <= 0xff))
        {
            // update font if needed
            mps_update_font();
            // handle rendering pdf char's that need esc'ing: "\", ")", "("
            if (c == ('\\') || c == '(' || c == ')')
                fwrite("\\", 1, 1, _file);
            fwrite(&c, 1, 1, _file);
            pdf_X += charWidth; // update x position
        }
        break;
    }
}
