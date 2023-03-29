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
    // there's also case for repeated bit image printing so need ESC-mode type processing

/**
 * TABLE 1 Page 26 of Users Manual
Special Control Character Summary
CHRSCode
CHRSI14)
CHRSM5)
CHR$|8)
CHRSCI8
CHRSM46)
CHRS(13)
CHRS(IO)
CHRS<17)
Printer function
Enhance ON
Enhance OF F
Bit Image Printing
Reverse ON
Reverse OFF
Carriage Return
Line Feed
Print in
Business Mode
Print in
Graphic Mode
Quote
Tab Setting
the Print Head
Repeat Graphic
Selected
Specify Dot Address
CHRSIH5)
CHRS(34>
CHRS[16); "nHnL"
CHR$(26J; CHRS|btt image data)
CHRS(27);CHRS(16);CHRS(nH)CHRS{nL)
'
i
*/

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
    
    // maybe printable character
    if (c > 31 && c < 127)
    {
        if (!sideFlag || c > 47)
        {
            if (c == ('\\') || c == '(' || c == ')')
                fwrite("\\", 1, 1, _file);
            fwrite(&c, 1, 1, _file);
        }
        else
        {
            if (c < 48)
                fwrite(" ", 1, 1, _file);
        }

        pdf_X += charWidth; // update x position
    }
}
