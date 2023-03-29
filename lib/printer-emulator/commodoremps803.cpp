#include "commodoremps803.h"

void commodoremps803::post_new_file()
{
    shortname = "mps803";

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

void commodoremps803::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
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
    Selected            CHR$(26); CHR$(bit image data)
Specify Dot Address     CHRS(27);CHR$(16);CHR$(nH)CHR$(nL)

*/

// font numbers:
// 1 - graphic regular (10 CPI)
// 2 - graphic enhanced (doublewide - 5 CPI)
// 3 - business regular (10 CPI)
// 4 - business enhanced (doublwide - 5 CPI)
// 5 - bit graphics

// determine comm's channel to set font family - "graphics" or "business"
// TODO: need special handling for when there's "local" change to business/graphic mode
// using special char's 17 or 145 then CR clears back to "global" aux1 setting
    if (aux1 == 0)
    {
        if (fontNumber == 3 || fontNumber == 4)
        {
            fontNumber -= 2;
            fprintf(_file, ")]TJ\n/F%1d 12 Tf [(", fontNumber);
            // charWidth = 14.4; //72.0 / 5.0;
            fontUsed[fontNumber - 1] = true;
        }
    }
    else if (aux1 == 7)
    {
        if (fontNumber == 1 || fontNumber == 2)
        {
            fontNumber += 2;
            fprintf(_file, ")]TJ\n/F%1d 12 Tf [(", fontNumber);
            // charWidth = 14.4; //72.0 / 5.0;
            fontUsed[fontNumber - 1] = true;
        }
    }

    // process the incoming character

    /* page 27 in user's manual -  enhanced (doublewide) mode:
    An ASCII CHRSI15) character cancels the character enhancement specified by
    preceding CHR3{14) character or ASCII CHRS(15) is Standard Character Mode.
    CHRS114) and CHRS{15) cancel later mentioned bit image graphic printing code
    [CHRS(8|]. And these control codes give a 1/6 inch line feed.
     */

    /* page 34 - switching graphic/business char sets inline
    CHRSC17} functions until CHR$(145) or carriage return is detected.
    CHRS(145) functions until CHRSU7] or carriage return is detected.
    */


    // maybe printable character
    if ((c >= 0x20 && c <= 0x7f) || (c >= 0xa0 && c <= 0xff))
    {
        // handle rendering pdf char's that need esc'ing: "\", ")", "("
        if (c == ('\\') || c == '(' || c == ')')
            fwrite("\\", 1, 1, _file);
        fwrite(&c, 1, 1, _file);
        pdf_X += charWidth; // update x position
    }
}
