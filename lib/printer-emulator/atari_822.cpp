#include "atari_822.h"

void atari822::post_new_file()
{
    shortname = "a822";

    pageWidth = 319.5;  // paper roll is 4 7/16" from page 4 of owners manual
    pageHeight = 792.0; // just use 11" for letter paper
    leftMargin = 15.75; // fit print width on page width
    bottomMargin = 0.0;

    printWidth = 288.0; // 4" wide printable area
    lineHeight = 12.0;  // 6 lines per inch
    charWidth = 7.2;    // 10 char per inch
    fontNumber = 1;
    fontSize = 12;

    pdf_header();
}

void atari822::pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2)
{
    // use PDF inline image to display line of graphics
    /*
  q
  240 0 0 1 18 750 cm
  BI
  /W 240
  /H 1
  /CS /G
  /BPC 1
  /D [1 0]
  /F /AHx
  ID
  00 00 00 00 00 00 3C 00 7E 00 7C 60 00 3C 00 18 3C 00 78 7C 18 63 7E 3C 00 7E 3C 00 18 3C
  >
  EI
  Q
  */

    // Atari 822 modes:
    // aux1 == 'N'   normal mode
    // aux1 == 'L'   graphics mode

    // was: if (cmdFrame.comnd == 'W' && !textMode)
    if (aux1 == 'N' && !textMode)
    {
        textMode = true;
        pdf_begin_text(pdf_Y); // open new text object
        pdf_new_line();        // start new line of text (string array)
    }
    // was: else if (cmdFrame.comnd == 'P' && textMode)
    else if (aux1 == 'L' && textMode)
    {
        textMode = false;
        if (!BOLflag)
            pdf_end_line();   // close out string array
        fprintf(_file, "ET\r\n"); // close out text object
    }

    if (!textMode && BOLflag)
    {
        fprintf(_file, "q\n %g 0 0 %g %g %g cm\r\n", printWidth, lineHeight / 10.0, leftMargin, pdf_Y);
        fprintf(_file, "BI\n /W 240\n /H 1\n /CS /G\n /BPC 1\n /D [1 0]\n /F /AHx\nID\r\n");
        BOLflag = false;
    }
    if (!textMode)
    {
        if (gfxNumber < 30)
            fprintf(_file, " %02X", c);

        gfxNumber++;

        if (gfxNumber == 40)
        {
            fprintf(_file, "\n >\nEI\nQ\r\n");
            pdf_Y -= lineHeight / 10.0;
            BOLflag = true;
            gfxNumber = 0;
        }
    }

    // TODO: looks like auto wrapped lines are 1 dot apart and EOL lines are 3 dots apart

    if (textMode && c == 12)
    {
        pdf_end_page();
        pdf_new_page();
    }

    // simple ASCII printer
    if (textMode && c > 31 && c < 127)
    {
        if (c == '\\' || c == '(' || c == ')')
            fwrite("\\", 1, 1, _file);
        fwrite(&c, 1, 1, _file);

        pdf_X += charWidth; // update x position
    }
}
