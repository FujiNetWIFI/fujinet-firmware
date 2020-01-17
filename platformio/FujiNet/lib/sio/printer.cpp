#include "printer.h"

static byte intlchar[27] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197};

//pdf routines
void sioPrinter::pdf_header()
{
  _file->printf("%%PDF-1.4\n");
  // first object: catalog of pages
  pdf_objCtr = 1;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("1 0 obj\n<</Type /Catalog /Pages 2 0 R>>\nendobj\n");
  // second object: one page
  pdf_objCtr++;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("2 0 obj\n<</Type /Pages /Kids [3 0 R] /Count 1>>\nendobj\n");
  // third object: page contents
  pdf_objCtr++;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("3 0 obj\n<</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 %d %d] /Contents [ ", pageWidth, pageHeight);
  //for (int i = 0; i < maxLines; i++)
 // {
//    _file->printf("%d 0 R ", i + 6);
    _file->printf("6 0 R ");
  //}
  _file->printf("]>>\nendobj\n");
  // fourth object: font catalog
  pdf_objCtr++;
  objLocations[pdf_objCtr] = _file->position();
  //line = ;
  _file->printf("4 0 obj\n<</Font <</F1 5 0 R>>>>\nendobj\n");
  // fifth object: font 1
  pdf_objCtr++;
  objLocations[pdf_objCtr] = _file->position();
  _file->printf("5 0 obj\n<</Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding>>\nendobj\n", fontName);
}

void sioPrinter::pdf_xref()
{
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

void sioPrinter::pdf_add_line(std::u16string S)
{
  std::string L = std::string(); //text string
  std::string U = std::string(); //underscore string
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
    //todo: create an underscore line before adding \ to text line
    if ((S[i] & 0xff) == LEFTPAREN || (S[i] & 0xff) == RIGHTPAREN || (S[i] & 0xff) == BACKSLASH)
    {
      L.push_back(BACKSLASH);
    }
    L.push_back(S[i] & 0xff);
    if (S[i] > 0xff)
      U.push_back(95);
    else
      U.push_back(32);
  }

#ifdef DEBUG_S
  BUG_UART.print("L length: ");
  BUG_UART.println(L.length());
  BUG_UART.print("U length: ");
  BUG_UART.println(U.length());
#endif
  int le = L.length();
#ifdef DEBUG_S
  BUG_UART.println("adding line: ");
  BUG_UART.println(L.c_str());
#endif

  if (pdf_lineCounter == 0)
  {
    pdf_objCtr++;
    objLocations[pdf_objCtr] = _file->position();
    _file->printf("%d 0 obj\n<</Length ", pdf_objCtr);
    idx_stream_length = _file->position();
    _file->printf("00000>>\nstream\n");
    idx_stream_start = _file->position();
    int yCoord = pageHeight - lineHeight + bottomMargin - pdf_lineCounter * lineHeight;
    _file->printf("BT\n/F1 %2d Tf %3d %3d Td\n", fontSize, leftMargin, yCoord);
  }
  _file->printf("0 -12 Td (");
  for (int i = 0; i < le; i++)
  {
    _file->write((byte)L[i]);
  }
  _file->printf(")Tj\n");
  // move underline stuff to here

  size_t last_ = U.find_last_of('_');
  le = ++last_;
  //le = U.length();
  if (le > 0)
  {
    _file->printf("%3d %3d Td\n(", 0, 0);
    for (int i = 0; i < le; i++)
    {
      _file->write((byte)U[i]);
    }

    // asdfasdfasdfs

    _file->printf(")Tj\n");
  }
  pdf_lineCounter++;
  if (pdf_lineCounter == maxLines)
  {
    _file->printf("ET\n");
    idx_stream_stop = _file->position();
    _file->printf("endstream\nendobj\n");
    size_t idx_temp = _file->position();
    _file->seek(idx_stream_length);
    _file->printf("%5u", (idx_stream_stop - idx_stream_start));
    _file->seek(idx_temp);
  }
}

std::u16string sioPrinter::buffer_to_string(byte *buffer)
{
  // Atari 1027 escape codes:
  // CTRL-O - start underscoring        14      - note in T1027.BAS there is a case of 27 14
  // CTRL-N - stop underscoring         15
  // ESC CTRL-Y - start underscoring    27  25
  // ESC CTRL-Z - stop underscoring     27  26
  // ESC CTRL-W - start international   27  23
  // ESC CTRL-X - stop international    27  24

  eolFlag = false;
  std::u16string out = std::u16string();
  for (int i = 0; i < BUFN; i++)
  {
    if (buffer[i] == EOL)
    {
      eolFlag = true;
      return out;
    }
    else if (escMode)
    {
      if (buffer[i] == 25) // || buffer[i] == 14)?
        uscoreFlag = true;
      if (buffer[i] == 26) // || buffer[i] == 15)?
        uscoreFlag = false;
      if (buffer[i] == 23)
        intlFlag = true;
      if (buffer[i] == 24)
        intlFlag = false;
      escMode = false;
    }
    else if (buffer[i] == 14)
      uscoreFlag = true;
    else if (buffer[i] == 15)
      uscoreFlag = false;
    else if (buffer[i] == 27)
      escMode = true;
    else if (intlFlag && (buffer[i] < 32 || buffer[i] == 123)) //|| buffer[i] == 96?
    {
      char16_t c;
      // not sure about ATASCII 96. Codes 28-31 are arrows and require the symbol font - more work needed.
      if (buffer[i] < 27)
        c = (char16_t)intlchar[buffer[i]];
      else if (buffer[i] == 123)
        c = 196;
      else
        c = 35;

      if (uscoreFlag)
        c += 0x0100; // underscore

      out.push_back(c);
    }
    //printable characters for 1027 Standard Set
    else if (buffer[i] > 31 && buffer[i] < 123)
    {
      char16_t c = buffer[i];
      if (uscoreFlag)
        c += 0x0100; // underscore
      out.push_back(c);
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
    uscoreFlag = false;
    intlFlag = false;
    escMode = false;
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
      pdf_add_line(std::u16string());
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
      std::u16string temp = buffer_to_string(buffer);
#ifdef DEBUG_S
      BUG_UART.print("processed buffer: ->");
      //BUG_UART.print((char)temp.c_str());
      BUG_UART.println("<-");
#endif
      output.append(temp);
      // make function to count printable chars
      if (eolFlag || output.length() > maxCols)
      {
        std::u16string what = std::u16string();
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
        //BUG_UART.print((char)what.c_str());
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
