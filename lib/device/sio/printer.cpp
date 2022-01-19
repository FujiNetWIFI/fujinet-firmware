#ifdef BUILD_ATARI

#include "../../include/atascii.h"
#include "printer.h"

#include "file_printer.h"
#include "html_printer.h"
#include "atari_820.h"
#include "atari_822.h"
#include "atari_825.h"
#include "svg_plotter.h"
#include "atari_1020.h"
#include "atari_1025.h"
#include "atari_1027.h"
#include "atari_1029.h"
#include "epson_80.h"
#include "epson_tps.h"
#include "atari_xmm801.h"
#include "atari_xdm121.h"
#include "okimate_10.h"
#include "png_printer.h"

#include "fnConfig.h"

#define SIO_PRINTERCMD_PUT 0x50
#define SIO_PRINTERCMD_WRITE 0x57
#define SIO_PRINTERCMD_STATUS 0x53

constexpr const char * const sioPrinter::printer_model_str[PRINTER_INVALID];

sioPrinter::~sioPrinter()
{
    delete _pptr;
}

// write for W commands
void sioPrinter::sio_write(uint8_t aux1, uint8_t aux2)
{
    /* 
  How many bytes the Atari will be sending us:
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
    uint8_t linelen;
    switch (aux1)
    {
    case 'N':
    case 'L':
        linelen = 40;
        break;
    case 'S':
        linelen = 29;
        break;
    case 'D':
        linelen = 20;
        break;
    default:
        linelen = 40;
    }

    memset(_buffer, 0, sizeof(_buffer)); // clear _buffer
    uint8_t ck = bus_to_peripheral(_buffer, linelen);

    if (ck == sio_checksum(_buffer, linelen))
    {
        if (linelen == 29)
        {
            for (int i = 0; i < (linelen / 2); i++)
            {
                uint8_t tmp = _buffer[i];
                _buffer[i] = _buffer[linelen - i - 1];
                _buffer[linelen - i - 1] = tmp;
                if (_buffer[i] == ATASCII_EOL)
                    _buffer[i] = ' ';
            }
            _buffer[linelen] = ATASCII_EOL;
        }
        // Copy the data to the printer emulator's buffer
        memcpy(_pptr->provideBuffer(), _buffer, linelen);

        if (_pptr->process(linelen, aux1, aux2))
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

/**
 * Print from CP/M, which is one character...at...a...time...
 */
void sioPrinter::print_from_cpm(uint8_t c)
{
    _last_ms = fnSystem.millis();
    _pptr->provideBuffer()[0] = c;
    _pptr->process(1, 0, 0);
}

// Status
void sioPrinter::sio_status()
{
    /*
  STATUS frame per the 400/800 OS ROM Manual
  Command Status
  Aux 1 Byte (typo says AUX2 uint8_t)
  Timeout
  Unused

  OS ROM Manual continues on Command Status uint8_t:
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
  The FLAG uint8_t contains information relating to the most recent
  command prior to the status request and some controller constants.
  The DATA WRITE Timeout equals the maximum time to print a
  line of data assuming worst case controller produced Timeout
  delay. This Timeout is associated with printer timeout
  discussed earlier. 
*/
    uint8_t status[4];

    status[0] = 0;
    status[1] = _lastaux1;
    status[2] = 5;
    status[3] = 0;

    bus_to_computer(status, sizeof(status), false);
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
        _pptr = new atari820;
        break;
    case PRINTER_ATARI_822:
        _pptr = new atari822;
        break;
    case PRINTER_ATARI_825:
        _pptr = new atari825;
        break;
    case PRINTER_ATARI_1020:
        _pptr = new atari1020;
        break;
    case PRINTER_ATARI_1025:
        _pptr = new atari1025;
        break;
    case PRINTER_ATARI_1027:
        _pptr = new atari1027;
        break;
    case PRINTER_ATARI_1029:
        _pptr = new atari1029;
        break;
    case PRINTER_ATARI_XMM801:
        _pptr = new xmm801;
        break;
    case PRINTER_ATARI_XDM121:
        _pptr = new xdm121;
        break;
    case PRINTER_EPSON:
        _pptr = new epson80;
        break;
    case PRINTER_EPSON_PRINTSHOP:
        _pptr = new epsonTPS;
        break;
    case PRINTER_OKIMATE10:
        _pptr = new okimate10;
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

// Constructor just sets a default printer type
sioPrinter::sioPrinter(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
    set_printer_type(print_type);
}

void sioPrinter::shutdown()
{
    if (_pptr != nullptr)
        _pptr->closeOutput();
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
            "Atari 825",
            "Atari 1020",
            "Atari 1025",
            "Atari 1027",
            "Atari 1029",
            "Atari XMM801",
            "Atari XDM121",
            "Epson 80",
            "Epson PrintShop",
            "Okimate 10",
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
void sioPrinter::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    if (!Config.get_printer_enabled())
        Debug_println("sioPrinter::disabled, ignoring");
    else
    {
        switch (cmdFrame.comnd)
        {
        case SIO_PRINTERCMD_PUT: // Needed by A822 for graphics mode printing
        case SIO_PRINTERCMD_WRITE:
            _lastaux1 = cmdFrame.aux1;
            _lastaux2 = cmdFrame.aux2;
            _last_ms = fnSystem.millis();
            sio_ack();
            sio_write(_lastaux1, _lastaux2);
            break;
        case SIO_PRINTERCMD_STATUS:
            _last_ms = fnSystem.millis();
            sio_ack();
            sio_status();
            break;
        default:
            sio_nak();
        }
    }
}

#endif /* BUILD_ATARI */