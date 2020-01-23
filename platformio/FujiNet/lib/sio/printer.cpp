#include "printer.h"

const byte intlchar[32] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 27, UPARROW, DOWNARROW, LEFTARROW, RIGHTARROW};

//pdf routines
void sioPrinter::pdf_header()
{
  _file->printf("%%PDF-1.4\n");
  // first object: catalog of pages
  pdf_objCtr = 1;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("1 0 obj\n<</Type /Catalog /Pages 2 0 R>>\nendobj\n");

  // 3rd & 4th object: font catalog
  pdf_objCtr = 3; // skip the page catalog, leave object 3 blank for ease now
  objLocations[pdf_objCtr] = _file->position();
  //line = ;
  _file->printf("3 0 obj\n<</Font <</F1 5 0 R /F2 6 0 R>>>>\nendobj\n");
  // 3rd & 4th object: font catalog
  //pdf_objCtr = 4; // skip the page catalog, leave object 3 blank for ease now
  //objLocations[pdf_objCtr] = _file->position();
  //line = ;
  //_file->printf("4 0 obj\n<</Font <</F2 6 0 R>>>>\nendobj\n");
  // fifth object: font 1
  pdf_objCtr = 5;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("5 0 obj\n<</Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding>>\nendobj\n");
  // fifth object: font 1
  pdf_objCtr = 6;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("6 0 obj\n<</Type /Font /Subtype /Type1 /BaseFont /Symbol /Encoding /WinAnsiEncoding>>\nendobj\n");
}

void sioPrinter::pdf_xref()
{
  objLocations[2] = _file->position(); // hard code page catalog as object #2
  _file->printf("2 0 obj\n<</Type /Pages /Kids [ ");
  for (int i = 0; i < pdf_pageCounter; i++)
  {
    _file->printf("%d 0 R ", pageObjects[i]);
  }
  _file->printf("] /Count %d>>\nendobj\n", pdf_pageCounter);
  size_t xref = _file->position();
  pdf_objCtr++;
  _file->printf("xref\n");
  _file->printf("0 %u\n", pdf_objCtr);
  _file->printf("0000000000 65535 f\n");
  for (int i = 1; i < pdf_objCtr; i++)
  {
    _file->printf("%010u 00000 n\n", objLocations[i]);
  }
  _file->printf("trailer <</Size %u/Root 1 0 R>>\n", pdf_objCtr);
  _file->printf("startxref\n");
  _file->printf("%u\n", xref);
  _file->printf("%%%%EOF\n");
}

void sioPrinter::pdf_new_page()
{ // open a new page
  pdf_objCtr++;
  pageObjects[pdf_pageCounter] = pdf_objCtr;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("%d 0 obj\n<</Type /Page /Parent 2 0 R /Resources 3 0 R /MediaBox [0 0 %g %g] /Contents [ ", pdf_objCtr, pageWidth, pageHeight);
  pdf_objCtr++; // increment for the contents stream object
  _file->printf("%d 0 R ", pdf_objCtr);
  _file->printf("]>>\nendobj\n");

  // open new content stream and text object
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("%d 0 obj\n<</Length ", pdf_objCtr);
  idx_stream_length = _file->position();
  _file->printf("00000>>\nstream\n");
  idx_stream_start = _file->position();
  _file->printf("BT\n");

  TOPflag = false;
  //if (pdf_pageCounter == 0)
  _file->printf("/F1 12 Tf\n");                        // set default font
  _file->printf("%g %g Td\n", leftMargin, pageHeight); // go to top of page
  // voffset = -lineHeight;     // set line spacing
  pdf_Y = pageHeight; // reset print roller to top of page
  pdf_X = 0;          // set carriage to LHS
  BOLflag = true;
}

void sioPrinter::pdf_end_page()
{
  // close text object & stream
  _file->printf("ET\n");
  idx_stream_stop = _file->position();
  _file->printf("endstream\nendobj\n");
  size_t idx_temp = _file->position();
  _file->seek(idx_stream_length);
  _file->printf("%5u", (idx_stream_stop - idx_stream_start));
  _file->seek(idx_temp);
  // set counters
  pdf_pageCounter++;
  TOPflag = true;
}

void sioPrinter::pdf_new_line()
{
  // position new line and start text string array
  _file->printf("0 %g Td [(", -lineHeight);
  pdf_X = 0; // reinforce?
  BOLflag = false;
}

void sioPrinter::pdf_end_line()
{
  _file->printf(")]TJ\n"); // close the line
  pdf_Y -= lineHeight;     // line feed
  pdf_X = 0;               // CR
  BOLflag = true;
}

void sioPrinter::pdf_handle_char(byte c)
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

void sioPrinter::pdf_add(std::string S)
{
  if (TOPflag)
    pdf_new_page();

  // loop through string
  for (int i = 0; i < S.length(); i++)
  {
    byte c = byte(S[i]);

    if (BOLflag)
      pdf_new_line();
    // do special case of new line and S==EOL later
    // if (c == EOL)
    //   {                        // skip a line space
    //     voffset -= lineHeight; // relative coord
    //     pdf_Y -= lineHeight;   // absolute coord
    //   }
    // else
    // {
    pdf_handle_char(c);

    // check for EOL or if at end of line and need automatic CR
    if ((c == EOL) || (pdf_X > (maxWidth - charWidth)))
      pdf_end_line();
#ifdef DEBUG_S
    printf("c: %3d  x: %6.2f  y: %6.2f  ", c, pdf_X, pdf_Y);
    printf("TOP: %s  ", TOPflag ? "true " : "false");
    printf("BOL: %s  ", BOLflag ? "true " : "false");
    printf("ESC: %s  ", escMode ? "true " : "false");
    printf("USC: %s  ", uscoreFlag ? "true " : "false");
    printf("INL: %s\n", intlFlag ? "true " : "false");
#endif
  }

  // reset line spacing - only needed in special case of !doing line and EOL
  // voffset = -lineHeight;

  // if wrote last line, then close the page
  if (pdf_Y < lineHeight)
    pdf_end_page();
}

void sioPrinter::initPrinter(File *f, paper_t ty)
{
  _file = f;
  paperType = ty;
  if (paperType == PDF)
  {
    uscoreFlag = false;
    intlFlag = false;
    escMode = false;
    pdf_Y = 0;
    pdf_X = 0;
    pdf_pageCounter = 0;
    pdf_header();
  }
}

void sioPrinter::initPrinter(File *f)
{
  initPrinter(f, PDF);
}

void sioPrinter::pageEject()
{
  if (paperType == PDF)
  {
    if (!BOLflag)
      pdf_end_line();
    // to do: close the text string array if !BOLflag
    if (!TOPflag || pdf_pageCounter == 0)
      pdf_end_page();
    pdf_xref();
  }
}

paper_t sioPrinter::getPaperType()
{
  return paperType;
}

void sioPrinter::writeBuffer(byte *B, int n)
{
  int i = 0;
  switch (paperType)
  {
  case RAW:
    for (i = 0; i < n; i++)
    {
      _file->write(B[i]);
    }
    break;
  case TRIM:
    while (i < n)
    {
      _file->write(B[i]);
      if (B[i] == EOL)
        break;
      i++;
    }
    break;
  case ASCII:
    while (i < n)
    {
      if (B[i] == EOL)
      {
        _file->printf("\n");
        break;
      }
      if (B[i] > 31 && B[i] < 127)
        _file->write(B[i]);
      i++;
    }
    break;
  case PDF:
  default:
    std::string output;
    while (i < n)
    {
      output.push_back(B[i]);
      if (B[i] == EOL)
        break;
      i++;
    }
    pdf_add(output);
  }
}

// write for W commands
void sioPrinter::sio_write()
{
  byte n = 40;
  byte ck;

  // to do: size buffer based on AUX1:
  // Auxiliary Bytes 1
  // Normal Print (40 Char/Line) = 0x4E
  // Sideways Print (16 Char/Line) = 0x53
  // Other values in the 400/800 OS Manual
  // Normal 0x4E 'N'    40 chars
  // Sideways 0x53 'S'  29 chars
  // Wide 0x57 'W'      "not supported"
  // And the XL/XE OS ROM

  if (cmdFrame.aux1 == 'N')
    n = 40;
  else if (cmdFrame.aux1 == 'S')
    n = 29;
  SIO_UART.readBytes(buffer, n);
#ifdef DEBUG_S
  for (int z = 0; z < n; z++)
  {
    BUG_UART.print(buffer[z], DEC);
    BUG_UART.print(" ");
  }
  BUG_UART.println();
#endif
  ck = SIO_UART.read(); // Read checksum
  //delayMicroseconds(350);
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(buffer, n))
  {
    writeBuffer(buffer, n);
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('C');
    yield();
  }
}

// Status
void sioPrinter::sio_status()
{
  byte status[4];
  byte ck;

  // status frame per Atari 820 service manual
  /* The printer controller will return a data frame to the computer
reflecting the status. The STATUS DATA frame is shown below:
DONE/ERROR
FLAG
AUX. BYTE 1 from last WRITE COMMAND
DATA WRITE TIMEOUT
CHECKSUM
The FLAG byte contains information relating to the most recent
command prior to the status request and some controller constants.
The DATA WRITE Timeout equals the maximum time to print a
line of data assuming worst case controller produced Timeout
delay. This Timeout is associated with printer timeout
discussed earlier.  */
  /* But from 400/800 OS ROM Manual
Command Status
Aux 1 Byte (typo says AUX2 byte)
Timeout
Unused

OS ROM Manual continues on Command Status:
bit 0 - invalid command frame
bit 1 - invalid data frame
bit 7 - intelligent controller (normally 0)
*/
  status[0] = 0;
  status[1] = lastAux1;
  status[2] = 5;
  status[3] = 0;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C');         // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

// Process command
void sioPrinter::sio_process()
{
  switch (cmdFrame.comnd)
  {
  case 'W':
    sio_write();
    lastAux1 = cmdFrame.aux1;
    break;
  case 'S':
    sio_status();
    break;
  }
  cmdState = WAIT;
  //cmdTimer = 0;
}
