#ifdef BUILD_H89

#include "printer.h"

#include <cstring>

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "led.h"

#include "atari_1020.h"
#include "atari_1025.h"
#include "file_printer.h"
#include "html_printer.h"
#include "svg_plotter.h"
#include "epson_80.h"
#include "epson_tps.h"
#include "okimate_10.h"
#include "png_printer.h"
#include "coleco_printer.h"

/* TODO
   - what is bpos used for?
   - set stream mode
   - handle stream
*/

std::string buf;
bool taskActive=false;

constexpr const char * const H89Printer::printer_model_str[PRINTER_INVALID];

// Constructor just sets a default printer type
H89Printer::H89Printer(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
    set_printer_type(print_type);
}

H89Printer::~H89Printer()
{
    //vTaskDelete(thPrinter);
    delete _pptr;
    _pptr = nullptr;
}

H89Printer::printer_type H89Printer::match_modelname(std::string model_name)
{
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(H89Printer::printer_model_str[i]) == 0)
            break;

    return (printer_type)i;
}

void H89Printer::process(uint32_t commanddata, uint8_t checksum)
{
    // cmdFrame.commanddata = commanddata;
    // cmdFrame.checksum = checksum;

    // switch (cmdFrame.comnd)
    // {
    // case 'W':
    //     write();
    //     break;
    // case 'S':
    //     status();
    //     break;
    // case 'X':
    //     stream();
    //     break;
    // default:
    //     Debug_printf("H89 process: unimplemented command: 0x%02x", cmdFrame.comnd);
    // }
}

void H89Printer::shutdown()
{
}

void H89Printer::set_printer_type(printer_type printer_type)
{
    // Destroy any current printer emu object
    if (_pptr != nullptr)
    {
        delete _pptr;
    }

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
    case PRINTER_ATARI_1020:
        _pptr = new atari1020;
        break;
    case PRINTER_ATARI_1025:
        _pptr = new atari1025;
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
    //_pptr->setEOLBypass(true);
    _pptr->setEOL(0x0d);
}

#endif /* NEW_TARGET */
