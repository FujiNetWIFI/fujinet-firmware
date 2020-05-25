#include "../../include/atascii.h"
#include "printer.h"

#include "file_printer.h"
#include "html_printer.h"
#include "atari_820.h"
#include "atari_822.h"
#include "atari_1025.h"
#include "atari_1027.h"
#include "epson_80.h"
#include "png_printer.h"

// write for W commands
void sioPrinter::sio_write()
{
    byte n = 40;
    byte ck;

    memset(_buffer, 0, n); // clear _buffer

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

    ck = sio_to_peripheral(_buffer, n);

    if (ck == sio_checksum(_buffer, n))
    {
        if (n == 29)
        { // reverse the _buffer and replace EOL with space
            // needed for PDF sideways printing on A820
            byte temp[29];
            memcpy(temp, _buffer, n);
            for (int i = 0; i < n; i++)
            {
                buffer[i] = temp[n - 1 - i];
                if (buffer[i] == ATASCII_EOL)
                    buffer[i] = ' ';
            }
            buffer[n++] = ATASCII_EOL;
        }
        for (int i = 0; i < n; i++)
        {
            _pptr->copyChar(_buffer[i], i);
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
    status[1] = _lastAux1;
    status[2] = 5;
    status[3] = 0;

    sio_to_computer(status, sizeof(status), false);
}

void sioPrinter::set_printer_type(sioPrinter::printer_type printer_type)
{
    // Destroy any current printer emu object
    delete _pptr;

    _ptype = printer_type;
    switch (printer_type)
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
    case PRINTER_EPSON:
        _pptr = new epson80;
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
        _ptype = PRINTER_FILE_TRIM;
        break;
    }

    _pptr->initPrinter(_storage);
}

/*
void sioPrinter::set_storage(FileSystem *fs)
{
    _storage = fs;
    _pptr->initPrinter(_storage);
}
*/

// Constructor just sets a default printer type
sioPrinter::sioPrinter(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
   set_printer_type(print_type);
}

/* Returns a printer type given a string model name
*/
sioPrinter::printer_type sioPrinter::match_modelname(std::string model_name)
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
            "Epson 80",
            "GRANTIC",
            "HTML printer",
            "HTML ATASCII printer"};
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(models[i]) == 0)
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
        _lastAux1 = cmdFrame.aux1;
        _last_ms = fnSystem.millis();
        sio_ack();
        sio_write();
        break;
    case 'S': // 0x53
        _last_ms = fnSystem.millis();
        sio_ack();
        sio_status();
        break;
    default:
        sio_nak();
    }
}
