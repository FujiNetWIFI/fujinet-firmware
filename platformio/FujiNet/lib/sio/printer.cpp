#include "printer.h"

//pdf routines
void sioPrinter::pdf_header()
{
  pdf_offset = _file->printf("%%PDF-1.4\n");
  // first object: catalog of pages
  pdf_objCtr = 1;
  objLocations[pdf_objCtr] = pdf_offset;
  pdf_offset = _file->printf("1 0 obj <</Type /Catalog /Pages 2 0 R>> endobj\n");
  // second object: one page
  pdf_objCtr++;
  objLocations[pdf_objCtr] = objLocations[pdf_objCtr - 1] + pdf_offset;
  pdf_offset = _file->printf("2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>> endobj\n");
  // third object: page contents
  pdf_objCtr++;
  objLocations[pdf_objCtr] = objLocations[pdf_objCtr - 1] + pdf_offset;
  pdf_offset = _file->printf("3 0 obj <</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 %d %d] /Contents [ ", pageWidth, pageHeight);
  for (int i = 0; i < maxLines; i++)
  {
    pdf_offset += _file->printf("%d 0 R ", i + 6);
  }
  pdf_offset += _file->printf("]>> endobj\n");
  // fourth object: font catalog
  pdf_objCtr++;
  objLocations[pdf_objCtr] = objLocations[pdf_objCtr - 1] + pdf_offset;
  //line = ;
  pdf_offset = _file->printf("4 0 obj <</Font <</F1 5 0 R>>>> endobj\n");
  // fifth object: font 1
  pdf_objCtr++;
  objLocations[pdf_objCtr] = objLocations[pdf_objCtr - 1] + pdf_offset;
  pdf_offset = _file->printf("5 0 obj <</Type /Font /Subtype /Type1 /BaseFont /%s>> endobj\n", fontName);
}

void sioPrinter::pdf_xref()
{
  int xref = objLocations[pdf_objCtr] + pdf_offset;
  pdf_objCtr++;
  _file->printf("xref\n");
  _file->printf("0 %d\n", pdf_objCtr);
  _file->printf("0000000000 65535 f\n");
  for (int i = 1; i < (maxLines + 5); i++)
  {
    _file->printf("%010d 00000 n\n", objLocations[i]);
  }
  _file->printf("trailer <</Size %d/Root 1 0 R>>\n", pdf_objCtr);
  _file->printf("startxref\n");
  _file->printf("%d\n", xref);
  _file->printf("%%%%EOF\n");
}

void sioPrinter::pdf_add_line(std::string S)
{
  std::string L = "";
  // escape special CHARs
  // \n       | LINE FEED (0Ah) (LF)
  // \r       | CARRIAGE RETURN (0Dh) (CR)
  // \t       | HORIZONTAL TAB (09h) (HT)
  // \b       | BACKSPACE (08h) (BS)
  // \f       | FORM FEED (FF)
  // \(       | LEFT PARENTHESIS (28h)
  // \)       | RIGHT PARENTHESIS (29h)
  // \\       | REVERSE SOLIDUS (5Ch) (Backslash)
  // \ddd     | Character code ddd (octal)
  for (int i = 0; i < S.length(); i++)
  {
    if (S[i] == LEFTPAREN || S[i] == RIGHTPAREN || S[i] == BACKSLASH)
    {
      L.append(1, BACKSLASH);
    }
    L.append(1, S[i]);
  }
  int le = L.length();
#ifdef DEBUG_S
  BUG_UART.println("adding line: ");
  BUG_UART.println(L.c_str());
#endif
  // to do: handle odd characters for fprintf, e.g., %,'," etc.
  pdf_objCtr++;
  objLocations[pdf_objCtr] = objLocations[pdf_objCtr - 1] + pdf_offset;
  pdf_offset = _file->printf("%d 0 obj <</Length %d>> stream\n", pdf_objCtr, 30 + le);
  int yCoord = pageHeight - lineHeight + bottomMargin - pdf_lineCounter * lineHeight;
  //this string right here vvvvvv is 30 chars long plus the length of the payload
  pdf_offset += _file->printf("BT /F1 %2d Tf %2d %3d Td (", fontSize, leftMargin, yCoord);
  pdf_offset += le;
  for (int i = 0; i < le; i++)
  {
    _file->write((byte)L[i]);
  }
  pdf_offset += _file->printf(")Tj ET\n");
  pdf_offset += _file->printf("endstream endobj\n");
  pdf_lineCounter++;
}

std::string sioPrinter::buffer_to_string(byte *buffer)
{
  // Atari 1027 escape codes:
  // CTRL-O - start underscoring        14      - note in T1027.BAS there is a case of 27 14
  // CTRL-N - stop underscoring         15
  // ESC CTRL-Y - start underscoring    27  25
  // ESC CTRL-Z - stop underscoring     27  26
  // ESC CTRL-W - start international   27  23
  // ESC CTRL-X - stop international    27  24

  eolFlag = false;
  std::string out = "";
  for (int i = 0; i < BUFN; i++)
  {
    if (buffer[i] == EOL)
    {
      eolFlag = true;
      return out;
    }
    else if (escMode)
    {
      if (buffer[i] == 25 || buffer[i] == 14)
        ulFlag = true;
      if (buffer[i] == 26 || buffer[i] == 15)
        ulFlag = false;
      if (buffer[i] == 23)
        intFlag = true;
      if (buffer[i] == 24)
        intFlag = false;
      escMode = false;
    }
    else if (buffer[i] == 14)
      ulFlag = true;
    else if (buffer[i] == 15)
      ulFlag = false;
    else if (buffer[i] == 27)
      escMode = true;
    else if (intFlag && (buffer[i] < 32 || buffer[i] == 96 || buffer[i] == 123))
    {
      out.append(1, 35);
    }
    //printable characters for 1027 Standard Set
    else if (buffer[i] > 31 && buffer[i] < 123)
    {
      out.append(1, buffer[i]);
    }
  }
  return out;
}

void sioPrinter::initPrinter(File *f, paper_t ty)
{
  _file = f;
  paperType = ty;
  if (paperType == PDF)
  {
    pdf_lineCounter = 0;
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
    while (pdf_lineCounter < maxLines)
    {
      pdf_add_line("");
    }
    pdf_xref();
  }
}

void sioPrinter::processBuffer(byte *B, int n)
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
  case PDF:
  default:
    if (pdf_lineCounter < maxLines)
    {
      std::string temp = buffer_to_string(buffer);
#ifdef DEBUG_S
      BUG_UART.print("processed buffer: ->");
      BUG_UART.print(temp.c_str());
      BUG_UART.println("<-");
#endif
      output.append(temp);
      // make function to count printable chars
      if (eolFlag || output.length() > maxCols)
      {
        std::string what;
        if (output.length() > maxCols)
        { //pick out substring to send and keep rest
          what = output.substr(0, maxCols);
          output.erase(0, maxCols);
        }
        else
        {
          what = output;
          output.clear();
        }
#ifdef DEBUG_S
        BUG_UART.print("new line: ->");
        BUG_UART.print(what.c_str());
        BUG_UART.println("<-");
#endif
        pdf_add_line(what);
      }
    }
  }
}

// write for W commands
void sioPrinter::sio_write()
{
  byte ck;
  SIO_UART.readBytes(buffer, BUFN);
#ifdef DEBUG_S
  for (int z = 0; z < BUFN; z++)
  {
    BUG_UART.print(buffer[z], DEC);
    BUG_UART.print(" ");
  }
  BUG_UART.println();
#endif
  ck = SIO_UART.read(); // Read checksum
  //delayMicroseconds(350);
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(buffer, BUFN))
  {
    processBuffer(buffer, BUFN);
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('C');
    yield();
  }
}

// Status
void sioPrinter::sio_status()
{
  byte status[4] = {0x01, 0x01, 0x01, 0x01};
  byte ck;

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
    break;
  case 'S':
    sio_status();
    break;
  }
  cmdState = WAIT;
  //cmdTimer = 0;
}
