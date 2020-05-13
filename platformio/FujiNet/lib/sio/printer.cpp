#include "html_printer.h"
#include "atari_1025.h"
#include "atari_1027.h"
#include "file_printer.h"
#include "png_printer.h"
#include "printer.h"

// Global printer object
sioPrinter sioP;

void atari820::pdf_handle_char(byte c)
{
    // Atari 820 modes:
    // aux1 == 40   normal mode
    // aux1 == 29   sideways mode
    if (my_sioP->lastAux1 == 'N' && sideFlag)
    {
        _file.printf(")]TJ\n/F1 12 Tf [(");
        fontNumber = 1;
        fontSize = 12;
        sideFlag = false;
    }
    else if (my_sioP->lastAux1 == 'S' && !sideFlag)
    {
        _file.printf(")]TJ\n/F2 12 Tf [(");
        fontNumber = 2;
        fontSize = 12;
        sideFlag = true;
        fontUsed[1] = true;
        // could increase charWidth, but not necessary to make this work. I force EOL.
    }

    // maybe printable character
    if (c > 31 && c < 127)
    {
        if (!sideFlag || c > 47)
        {
            if (c == ('\\') || c == '(' || c == ')')
                _file.write('\\');
            _file.write(c);
        }
        else
        {
            if (c < 48)
                _file.write(' ');
        }

        pdf_X += charWidth; // update x position
    }
}

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
    // aux1 == 'N'   normal mode
    // aux1 == 'L'   graphics mode

    // was: if (cmdFrame.comnd == 'W' && !textMode)
    if (my_sioP->lastAux1 == 'N' && !textMode)
    {
        textMode = true;
        pdf_begin_text(pdf_Y); // open new text object
        pdf_new_line();        // start new line of text (string array)
    }
    // was: else if (cmdFrame.comnd == 'P' && textMode)
    else if (my_sioP->lastAux1 == 'L' && textMode)
    {
        textMode = false;
        if (!BOLflag)
            pdf_end_line();   // close out string array
        _file.printf("ET\n"); // close out text object
    }

    if (!textMode && BOLflag)
    {
        _file.printf("q\n %g 0 0 %g %g %g cm\n", printWidth, lineHeight / 10.0, leftMargin, pdf_Y);
        _file.printf("BI\n /W 240\n /H 1\n /CS /G\n /BPC 1\n /D [1 0]\n /F /AHx\nID\n");
        BOLflag = false;
    }
    if (!textMode)
    {
        if (gfxNumber < 30)
            _file.printf(" %02X", c);

        gfxNumber++;

        if (gfxNumber == 40)
        {
            _file.printf("\n >\nEI\nQ\n");
            pdf_Y -= lineHeight / 10.0;
            BOLflag = true;
            gfxNumber = 0;
        }
    }

    // TODO: looks like auto wrapped lines are 1 dot apart and EOL lines are 3 dots apart

    // simple ASCII printer
    if (textMode && c > 31 && c < 127)
    {
        if (c == '\\' || c == '(' || c == ')')
            _file.write('\\');
        _file.write(c);

        pdf_X += charWidth; // update x position
    }
}

void atari820::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);

    shortname = "a820";

    pageWidth = 279.0;  // paper roll is 3 7/8" from page 6 of owners manual
    pageHeight = 792.0; // just use 11" for letter paper
    leftMargin = 19.5;  // fit print width on page width
    bottomMargin = 0.0;
    // dimensions from Table 1-1 of Atari 820 Field Service Manual
    printWidth = 240.0; // 3 1/3" wide printable area
    lineHeight = 12.0;  // 6 lines per inch
    charWidth = 6.0;    // 12 char per inch
    fontNumber = 1;
    fontSize = 12;
    sideFlag = false;

    pdf_header();
}

void atari822::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);

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

  Double   0x44 'D'  20 chars (XL OS Source)

  Atari 822 in graphics mode (SIO command 'P') 
           0x50 'L'  40 bytes
  as inferred from screen print program in operators manual

  Auxiliary Byte 2 for Atari 822 might be 0 or 1 in graphics mode
*/
    // todo: change to switch case structure
    if (cmdFrame.aux1 == 'N' || cmdFrame.aux1 == 'L')
        n = 40;
    else if (cmdFrame.aux1 == 'S')
        n = 29;
    else if (cmdFrame.aux1 == 'D')
        n = 20;

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
        for (int i = 0; i < n; i++)
        {
            _pptr->copyChar(buffer[i], i);
        }
        if (_pptr->process(n))
            sio_complete();
        else
        {
            sio_error();
        }
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

void sioPrinter::set_printer_type(sioPrinter::printer_type t)
{
    // Destroy any current printer emu object
    delete _pptr;

    pt = t;
    switch (t)
    {
    case PRINTER_FILE_RAW:
        _pptr = new filePrinter(RAW);
        break;
    case PRINTER_FILE_TRIM:
        _pptr = new filePrinter;
        break;
    case PRINTER_FILE_ASCII:
        _pptr = new filePrinter(ASCII);
        break;
    case PRINTER_ATARI_820:
        _pptr = new atari820(this);
        break;
    case PRINTER_ATARI_822:
        _pptr = new atari822(this);
        break;
    case PRINTER_ATARI_1025:
        _pptr = new atari1025;
        break;
    case PRINTER_ATARI_1027:
        _pptr = new atari1027;
        break;
    case PRINTER_PNG:
        _pptr = new pngPrinter;
        break;
    case PRINTER_HTML:
        _pptr = new htmlPrinter;
        break;
    case PRINTER_HTML_ATASCII:
        _pptr = new htmlPrinter(HTML_ATASCII);
        break;
    default:
        _pptr = new filePrinter;
        pt = PRINTER_FILE_TRIM;
        break;
    }

    _pptr->initPrinter(_storage);
}

void sioPrinter::set_storage(FS *fs)
{
    _storage = fs;
    _pptr->initPrinter(_storage);
}

// Constructor just sets a default printer type
sioPrinter::sioPrinter()
{
    _pptr = new filePrinter;
}

/* Returns a printer type given a string model name
*/
sioPrinter::printer_type sioPrinter::match_modelname(std::string modelname)
{
    const char *models[PRINTER_INVALID] =
        {
            "file printer (RAW)",
            "file printer (TRIM)",
            "file printer (ASCII)",
            "Atari 820",
            "Atari 822",
            "Atari 1025",
            "Atari 1027",
            "GRANTIC",
            "HTML printer",
            "HTML ATASCII printer"};
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (modelname.compare(models[i]) == 0)
            break;

    return (printer_type)i;
}

// Process command
void sioPrinter::sio_process()
{
    switch (cmdFrame.comnd)
    {
    case 'P': // 0x50 - needed by A822 for graphics mode printing
    case 'W': // 0x57
        lastAux1 = cmdFrame.aux1;
        last_ms = fnSystem.millis();
        sio_ack();
        sio_write();
        break;
    case 'S': // 0x53
        last_ms = fnSystem.millis();
        sio_ack();
        sio_status();
        break;
    default:
        sio_nak();
    }
}
