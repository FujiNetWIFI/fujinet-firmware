#include "printer.h"

//pdf routines
void pdfPrinter::pdf_header()
{
  pdf_Y = 0;
  pdf_X = 0;
  pdf_pageCounter = 0;
  _file->printf("%%PDF-1.4\n");
  // first object: catalog of pages
  pdf_objCtr = 1;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("1 0 obj\n<</Type /Catalog /Pages 2 0 R>>\nendobj\n");
  // object 2 0 R is printed at bottom of PDF before xref
  this->pdf_fonts(); // pdf_fonts is virtual function, call by pointer
}

void asciiPrinter::pdf_fonts()
{
  // 3rd object: font catalog
  pdf_objCtr = 3;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("3 0 obj\n<</Font << /F1 4 0 R >>>>\nendobj\n");

  // 1027 standard font
  pdf_objCtr = 4;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("4 0 obj\n<</Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding>>\nendobj\n");
}

void atari1027::pdf_fonts()
/*
  5 0 obj
  <</Type/Font/Subtype/TrueType/Name/F1/BaseFont/PrestigeEliteNormal/Encoding/WinAnsiEncoding/FontDescriptor 6 0 R/FirstChar 32/LastChar 252/Widths 7 0 R>>
  endobj
  6 0 obj
  <</Type/FontDescriptor/FontName/PrestigeEliteNormal/Flags 33/ItalicAngle 0/Ascent 662/Descent -216/CapHeight 662/AvgWidth 600/MaxWidth 600/FontWeight 400/XHeight 250/StemV 60/FontBBox[ -2 -216 625 662] >>
  endobj
  7 0 obj
  [ 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600
    600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600
    600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600
    600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600
    600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600
    600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600
    600 600 600 600 600 600 600 600]
  endobj
*/
{
  // 3rd object: font catalog
  pdf_objCtr = 3;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("3 0 obj\n<</Font <</F1 4 0 R /F2 7 0 R>>>>\nendobj\n");

  // 1027 standard font
  pdf_objCtr = 4;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("4 0 obj\n<</Type/Font/Subtype/TrueType/Name/F1/BaseFont/PrestigeEliteNormal/Encoding/WinAnsiEncoding/FontDescriptor 5 0 R/FirstChar 32/LastChar 252/Widths 6 0 R>>\nendobj\n");
  pdf_objCtr = 5;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("5 0 obj\n<</Type/FontDescriptor/FontName/PrestigeEliteNormal/Flags 33/ItalicAngle 0/Ascent 662/Descent -216/CapHeight 662/AvgWidth 600/MaxWidth 600/FontWeight 400/XHeight 250/StemV 60/FontBBox[ -2 -216 625 662]>>\nendobj\n");
  pdf_objCtr = 6;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("6 0 obj\n[");
  for (int i = 32; i < 253; i++)
  {
    _file->printf(" 600");
    if ((i - 31) % 32 == 0)
      _file->printf("\n");
  }
  _file->printf(" ]\nendobj\n");

  // symbol font to put in arrows
  pdf_objCtr = 7;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("7 0 obj\n<</Type /Font /Subtype /Type1 /BaseFont /Symbol /Encoding /WinAnsiEncoding>>\nendobj\n");
}

void atari820::pdf_fonts()
{
  // 3rd object: font catalog
  pdf_objCtr = 3;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("3 0 obj\n<</Font << /F1 4 0 R /F2 7 0 R >>>>\nendobj\n");

  // 820 standard font
  pdf_objCtr = 4;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("4 0 obj\n<</Type/Font/Subtype/TrueType/Name/F1/BaseFont/mono5by7ascii500w/Encoding/WinAnsiEncoding/FontDescriptor 5 0 R/FirstChar 32/LastChar 127/Widths 6 0 R>>\nendobj\n");
  pdf_objCtr = 5;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("5 0 obj\n<</Type/FontDescriptor/FontName/mono5by7ascii500w/Flags 33/ItalicAngle 0/Ascent 700/Descent 0/CapHeight 700/AvgWidth 500/MaxWidth 500/FontWeight 400/XHeight 500/StemV 55.9/FontBBox[33 -1 463 717] >>\nendobj\n");
  pdf_objCtr = 6;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("6 0 obj\n[");
  for (int i = 32; i < 128; i++)
  {
    _file->printf(" 500");
    if ((i - 31) % 32 == 0)
      _file->printf("\n");
  }
  _file->printf(" ]\nendobj\n");

  // 820 sideways font
  pdf_objCtr = 7;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("7 0 obj\n<</Type/Font/Subtype/TrueType/Name/F2/BaseFont/mono5by7asciiSideways/Encoding/WinAnsiEncoding/FontDescriptor 8 0 R/FirstChar 32/LastChar 127/Widths 9 0 R>>\nendobj\n");
  pdf_objCtr = 8;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("8 0 obj\n<</Type/FontDescriptor/FontName/mono5by7asciiSideways/Flags 33/ItalicAngle 0/Ascent 700/Descent 0/CapHeight 700/AvgWidth 675/MaxWidth 675/FontWeight 400/XHeight 500/StemV 77/FontBBox[41 -294 634 733] >>\nendobj\n");
  pdf_objCtr = 9;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("9 0 obj\n[");
  for (int i = 32; i < 128; i++)
  {
    _file->printf(" 675");
    if ((i - 31) % 32 == 0)
      _file->printf("\n");
  }
  _file->printf(" ]\nendobj\n");
}

void atari822::pdf_fonts()
{
  // 3rd object: font catalog
  pdf_objCtr = 3;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("3 0 obj\n<</Font << /F1 4 0 R >>>>\nendobj\n");

  // 822 font
  pdf_objCtr = 4;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("4 0 obj\n<</Type/Font/Subtype/TrueType/Name/F1/BaseFont/5x7-Monospace-CE/Encoding/WinAnsiEncoding/FontDescriptor 5 0 R/FirstChar 32/LastChar 127/Widths 6 0 R>>\nendobj\n");
  pdf_objCtr = 5;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("5 0 obj\n<</Type/FontDescriptor/FontName/5x7-Monospace-CE/Flags 33/ItalicAngle 0/Ascent 1000/Descent 0/CapHeight 875.0/AvgWidth 750/MaxWidth 750/FontWeight 400/XHeight 625.0/StemV 87.4707/FontBBox[0.0 0.0 672.85156 1000.0] >>\nendobj\n");
  pdf_objCtr = 6;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("6 0 obj\n[");
  for (int i = 32; i < 128; i++)
  {
    _file->printf(" 750");
    if ((i - 31) % 32 == 0)
      _file->printf("\n");
  }
  _file->printf(" ]\nendobj\n");
}

void pdfPrinter::pdf_xref()
{
  int max_objCtr = pdf_objCtr;
  pdf_objCtr = 2;
  objLocations[pdf_objCtr] = _file->position(); // hard code page catalog as object #2
  _file->printf("2 0 obj\n<</Type /Pages /Kids [ ");
  for (int i = 0; i < pdf_pageCounter; i++)
  {
    _file->printf("%d 0 R ", pageObjects[i]);
  }
  _file->printf("] /Count %d>>\nendobj\n", pdf_pageCounter);
  size_t xref = _file->position();
  max_objCtr++;
  _file->printf("xref\n");
  _file->printf("0 %u\n", max_objCtr);
  _file->printf("0000000000 65535 f\n");
  for (int i = 1; i < max_objCtr; i++)
  {
    _file->printf("%010u 00000 n\n", objLocations[i]);
  }
  _file->printf("trailer <</Size %u/Root 1 0 R>>\n", max_objCtr);
  _file->printf("startxref\n");
  _file->printf("%u\n", xref);
  _file->printf("%%%%EOF\n");
}

void pdfPrinter::pdf_begin_text(double Y)
{
  // open new text object
  _file->printf("BT\n");
  TOPflag = false;
  _file->printf("/F%u %u Tf\n", fontNumber, fontSize);
  _file->printf("%g %g Td\n", leftMargin, Y);
  pdf_Y = Y; // reset print roller to top of page
  pdf_X = 0; // set carriage to LHS
  BOLflag = true;
}

void pdfPrinter::pdf_new_page()
{ // open a new page
  pdf_objCtr++;
  pageObjects[pdf_pageCounter] = pdf_objCtr;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("%d 0 obj\n<</Type /Page /Parent 2 0 R /Resources 3 0 R /MediaBox [0 0 %g %g] /Contents [ ", pdf_objCtr, pageWidth, pageHeight);
  pdf_objCtr++; // increment for the contents stream object
  _file->printf("%d 0 R ", pdf_objCtr);
  _file->printf("]>>\nendobj\n");

  // open content stream
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("%d 0 obj\n<</Length ", pdf_objCtr);
  idx_stream_length = _file->position();
  _file->printf("00000>>\nstream\n");
  idx_stream_start = _file->position();

  // open new text object
  pdf_begin_text(pageHeight);
}

void pdfPrinter::pdf_end_page()
{
  // close text object & stream
  if (!BOLflag)
    pdf_end_line();
  _file->printf("ET\n");
  idx_stream_stop = _file->position();
  _file->printf("endstream\nendobj\n");
  size_t idx_temp = _file->position();
  _file->flush();
  _file->seek(idx_stream_length);
  _file->printf("%5u", (idx_stream_stop - idx_stream_start));
  _file->flush();
  _file->seek(idx_temp);
  // set counters
  pdf_pageCounter++;
  TOPflag = true;
}

void pdfPrinter::pdf_new_line()
{
  // position new line and start text string array
  _file->printf("0 %g Td [(", -lineHeight);
  pdf_X = 0; // reinforce?
  BOLflag = false;
}

void pdfPrinter::pdf_end_line()
{
  _file->printf(")]TJ\n"); // close the line
  pdf_Y -= lineHeight;     // line feed
  pdf_X = 0;               // CR
  BOLflag = true;
}

void asciiPrinter::pdf_handle_char(byte c)
{
  // simple ASCII printer
  if (c > 31 && c < 127)
  {
    if (c == BACKSLASH || c == LEFTPAREN || c == RIGHTPAREN)
      _file->write(BACKSLASH);
    _file->write(c);

    pdf_X += charWidth; // update x position
  }
}

void atari820::pdf_handle_char(byte c)
{
  // Atari 820 modes:
  // aux1 == 40   normal mode
  // aux1 == 29   sideways mode
  if (cmdFrame.aux1 == 'N' && sideFlag)
  {
    _file->printf(")]TJ\n/F1 12 Tf [(");
    sideFlag = false;
  }
  else if (cmdFrame.aux1 == 'S' && !sideFlag)
  {
    _file->printf(")]TJ\n/F2 12 Tf [(");
    sideFlag = true;
    // could increase charWidth, but not necessary to make this work. I force EOL.
  }

  // maybe printable character
  if (c > 31 && c < 127)
  {
    if (!sideFlag || c > 47)
    {
      if (c == BACKSLASH || c == LEFTPAREN || c == RIGHTPAREN)
        _file->write(BACKSLASH);
      _file->write(c);
    }
    else
    {
      if (c < 48)
        _file->write(' ');
    }

    pdf_X += charWidth; // update x position
  }
}

//=====================================================================================

void atari822::pdf_handle_char(byte c)
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
  // command == 'W'   normal mode
  // command == 'P'   graphics mode
  if (cmdFrame.comnd == 'W' && !textMode)
  {
    textMode = true;
    pdf_begin_text(pdf_Y); // open new text object
    pdf_new_line();        // start new line of text (string array)
  }
  else if (cmdFrame.comnd == 'P' && textMode)
  {
    textMode = false;
    if (!BOLflag)
      pdf_end_line();      // close out string array
    _file->printf("ET\n"); // close out text object
  }

  if (!textMode && BOLflag)
  {
    _file->printf("q\n %g 0 0 %g %g %g cm\n", printWidth, lineHeight / 10.0, leftMargin, pdf_Y);
    _file->printf("BI\n /W 240\n /H 1\n /CS /G\n /BPC 1\n /D [1 0]\n /F /AHx\nID\n");
    BOLflag = false;
  }
  if (!textMode)
  {
    if (gfxNumber < 30)
      _file->printf(" %02X", c);

    gfxNumber++;

    if (gfxNumber == 40)
    {
      _file->printf("\n >\nEI\nQ\n");
      pdf_Y -= lineHeight / 10.0;
      BOLflag = true;
      gfxNumber = 0;
    }
  }

  // TODO: looks like auto wrapped lines are 1 dot apart and EOL lines are 3 dots apart

  // simple ASCII printer
  if (textMode && c > 31 && c < 127)
  {
    if (c == BACKSLASH || c == LEFTPAREN || c == RIGHTPAREN)
      _file->write(BACKSLASH);
    _file->write(c);

    pdf_X += charWidth; // update x position
  }
}

//=====================================================================================

void atari1027::pdf_handle_char(byte c)
{
  if (escMode)
  {
    // Atari 1027 escape codes:
    // CTRL-O - start underscoring        15
    // CTRL-N - stop underscoring         14  - note in T1027.BAS there is a case of 27 14
    // ESC CTRL-Y - start underscoring    27  25
    // ESC CTRL-Z - stop underscoring     27  26
    // ESC CTRL-W - start international   27  23
    // ESC CTRL-X - stop international    27  24
    if (c == 25)
      uscoreFlag = true;
    if (c == 26)
      uscoreFlag = false;
    if (c == 23)
      intlFlag = true;
    if (c == 24)
      intlFlag = false;
    escMode = false;
  }
  else if (c == 15)
    uscoreFlag = true;
  else if (c == 14)
    uscoreFlag = false;
  else if (c == 27)
    escMode = true;
  else
  { // maybe printable character
    //printable characters for 1027 Standard Set + a few more >123 -- see mapping atari on ATASCII
    if (intlFlag && (c < 32 || c == 123))
    {
      // not sure about ATASCII 96.
      // todo: Codes 28-31 are arrows and require the symbol font
      if (c < 27)
        _file->write(intlchar[c]);
      else if (c == 123)
        _file->write(byte(196));
      else if (c > 27 && c < 32)
      {
        //_file->printf(")]TJ\n/F2 12 Tf (");
        _file->write(intlchar[c]); // Symbol font is not monospace
        //_file->printf(")Tj\n/F1 12 Tf\n[(");
      }

      pdf_X += charWidth; // update x position
    }
    else if (c > 31 && c < 127)
    {
      if (c == BACKSLASH || c == LEFTPAREN || c == RIGHTPAREN)
        _file->write(BACKSLASH);
      _file->write(c);

      if (uscoreFlag)
        _file->printf(")600(_"); // close text string, backspace, start new text string, write _

      pdf_X += charWidth; // update x position
    }
  }
}

void pdfPrinter::pdf_add(std::string S)
{
  // algorithm for graphics:
  // if textMode, then can do the regular stuff
  // if !textMode, then don't deal with BOL, EOL.
  // check for TOP always just in case.
  // can graphics mode ignore over-flowing a page for now?
  // textMode is set inside of pdf_handle_char at first character, so...
  // need to test for textMode inside the loop

  if (TOPflag)
    pdf_new_page();

  // loop through string
  for (int i = 0; i < S.length(); i++)
  {
    byte c = byte(S[i]);

    if (!textMode)
    {
      this->pdf_handle_char(c);
    }
    else
    {
      if (BOLflag && c == EOL)
        pdf_new_line();

      // check for EOL or if at end of line and need automatic CR
      if (!BOLflag && ((c == EOL) || (pdf_X > (printWidth - charWidth))))
        pdf_end_line();

      // start a new line if we need to
      if (BOLflag)
        pdf_new_line();

      // disposition the current byte
      this->pdf_handle_char(c);

#ifdef DEBUG
      Debug_printf("c: %3d  x: %6.2f  y: %6.2f  ", c, pdf_X, pdf_Y);
      Debug_printf("\n");
#endif
    }
  }

  // if wrote last line, then close the page
  if (pdf_Y < lineHeight + bottomMargin)
    pdf_end_page();
}

// void asciiPrinter::svg_add(std::string S)
// {
// }

void filePrinter::initPrinter(File *f)
{
  _file = f;
}

void filePrinter::setPaper(paper_t ty)
{
  paperType = ty;
}

void asciiPrinter::initPrinter(File *f)
{
  _file = f;
  paperType = PDF;
  pdf_header();
}

void atari1027::initPrinter(File *f)
{
  // todo: put page parameter assignments here
  _file = f;
  paperType = PDF;
  uscoreFlag = false;
  intlFlag = false;
  escMode = false;
  pdf_header();
}

void atari820::initPrinter(File *f)
{
  _file = f;
  paperType = PDF;
  pageWidth = 279.0;  // paper roll is 3 7/8" from page 6 of owners manual
  pageHeight = 792.0; // just use 11" for letter paper
  leftMargin = 19.5;  // fit print width on page width
  bottomMargin = 0.0;
  // dimensions from Table 1-1 of Atari 820 Field Service Manual
  printWidth = 240.0; // 3 1/3" wide printable area
  lineHeight = 12.0;  // 6 lines per inch
  charWidth = 6.0;    // 12 char per inch
  fontSize = 12;      // 6 lines per inch

  sideFlag = false;
  pdf_header();
}

void atari822::initPrinter(File *f)
{
  _file = f;
  paperType = PDF;
  pageWidth = 319.5;  // paper roll is 4 7/16" from page 4 of owners manual
  pageHeight = 792.0; // just use 11" for letter paper
  leftMargin = 15.75; // fit print width on page width
  bottomMargin = 0.0;

  printWidth = 288.0; // 4" wide printable area
  lineHeight = 12.0;  // 6 lines per inch
  charWidth = 7.2;    // 10 char per inch
  fontSize = 10;      // 10 char per inch for close font

  pdf_header();
}

void atari1020::initPrinter(File *f)
{
  _file = f;
  paperType = SVG;

  textFlag = true;
  svg_header();
}

void atari1020::svg_header()
{
  //fprintf(f,"<!DOCTYPE html>\n");
  //fprintf(f,"<html>\n");
  //fprintf(f,"<body>\n\n");
  _file->printf("<svg height=\"2000\" width=\"480\" viewBox=\"0 -1000 480 2000\">\n");
}

void pdfPrinter::pageEject()
{
  if (paperType == PDF)
  {
    if (TOPflag && pdf_pageCounter == 0)
      pdf_new_page();
    if (!BOLflag)
      pdf_end_line();
    // to do: close the text string array if !BOLflag
    if (!TOPflag || pdf_pageCounter == 0)
      pdf_end_page();
    pdf_xref();
  }
}

void filePrinter::writeBuffer(byte *B, int n)
{
  int i = 0;
  std::string output = std::string();

  switch (paperType)
  {
  case RAW:
    for (i = 0; i < n; i++)
    {
      _file->write(B[i]);
      Debug_print(B[i], HEX);
    }
    Debug_printf("\n");
    break;
  case TRIM:
    while (i < n)
    {
      _file->write(B[i]);
      Debug_print(B[i], HEX);
      if (B[i] == EOL)
      {
        Debug_printf("\n");
        break;
      }
      i++;
    }
    break;
  case ASCII:
  default:
    while (i < n)
    {
      if (B[i] == EOL)
      {
        _file->printf("\n");
        Debug_printf("\n");
        break;
      }
      if (B[i] > 31 && B[i] < 127)
      {
        _file->write(B[i]);
        Debug_printf("%c", B[i]);
      }
      i++;
    }
  }
}

void pdfPrinter::writeBuffer(byte *B, int n)
{
  int i = 0;
  std::string output = std::string();

  while (i < n)
  {
    output.push_back(B[i]);
    if (B[i] == EOL)
      break;
    i++;
  }
  pdf_add(output);
}

paper_t sioPrinter::getPaperType()
{
  return paperType;
}

// write for W commands
void sioPrinter::sio_write()
{
  byte n = 40;
  byte ck;

  memset(buffer, 0, n); // clear buffer

  /* 
  Auxiliary Byte 1 values per 400/800 OS Manual
  Normal   0x4E 'N'  40 chars
  Sideways 0x53 'S'  29 chars (820 sideways printing)
  Wide     0x57 'W'  "not supported"

  Atari 822 in graphics mode (SIO command 'P') 
           0x50 'L'  40 bytes
  as inferred from screen print program in operators manual

  Auxiliary Byte 2 for Atari 822 might be 0 or 1 in graphics mode
*/

  if (cmdFrame.aux1 == 'N')
    n = 40;
  else if (cmdFrame.aux1 == 'S')
    n = 29;

  ck = sio_to_peripheral(buffer, n);

  if (ck == sio_checksum(buffer, n))
  {
    if (n == 29)
    { // reverse the buffer and replace EOL with space
      // needed for PDF sideways printing on A820
      byte temp[29];
      memcpy(temp, buffer, n);
      for (int i = 0; i < n; i++)
      {
        buffer[i] = temp[n - 1 - i];
        if (buffer[i] == EOL)
          buffer[i] = ' ';
      }
      buffer[n++] = EOL;
    }
    writeBuffer(buffer, n);
    sio_complete();
  }
  else
  {
    sio_error();
  }
}

// Status
void sioPrinter::sio_status()
{
  byte status[4];
  /*
  STATUS frame per the 400/800 OS ROM Manual
  Command Status
  Aux 1 Byte (typo says AUX2 byte)
  Timeout
  Unused

  OS ROM Manual continues on Command Status byte:
  bit 0 - invalid command frame
  bit 1 - invalid data frame
  bit 7 - intelligent controller (normally 0)

  STATUS frame per Atari 820 service manual
  The printer controller will return a data frame to the computer
  reflecting the status. The STATUS DATA frame is shown below:
  DONE/ERROR FLAG
  AUX. BYTE 1 from last WRITE COMMAND
  DATA WRITE TIMEOUT
  CHECKSUM
  The FLAG byte contains information relating to the most recent
  command prior to the status request and some controller constants.
  The DATA WRITE Timeout equals the maximum time to print a
  line of data assuming worst case controller produced Timeout
  delay. This Timeout is associated with printer timeout
  discussed earlier. 
*/

  status[0] = 0;
  status[1] = lastAux1;
  status[2] = 5;
  status[3] = 0;

  sio_to_computer(status, sizeof(status), false);
}

// Process command
void sioPrinter::sio_process()
{
  switch (cmdFrame.comnd)
  {
  case 'P': // 0x50 - needed by A822 for graphics mode printing
  case 'W': // 0x57
    sio_ack();
    sio_write();
    lastAux1 = cmdFrame.aux1;
    break;
  case 'S': // 0x53
    sio_ack();
    sio_status();
    break;
  default:
    sio_nak();
  }
  // cmdState = WAIT;
  //cmdTimer = 0;
}
