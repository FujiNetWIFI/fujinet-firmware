#ifdef BUILD_COCO

#include "printer.h"

#include <cstring>

#include "../../include/debug.h"
#include "../../include/atascii.h"

#include "fnSystem.h"
#include "fnConfig.h"

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

constexpr const char * const drivewirePrinter::printer_model_str[PRINTER_INVALID];

drivewirePrinter::~drivewirePrinter()
{
    if (_pptr != nullptr)
    {
        delete _pptr;
        _pptr = nullptr;
    }
}

// write for W commands
void drivewirePrinter::write(uint8_t c)
{
    _last_ms = fnSystem.millis();
    _pptr->provideBuffer()[0] = c;
    _pptr->process(1, 0, 0);
}

/**
 * Print from CP/M, which is one character...at...a...time...
 */
void drivewirePrinter::print_from_cpm(uint8_t c)
{
    _last_ms = fnSystem.millis();
    _pptr->provideBuffer()[0] = c;
    _pptr->process(1, 0, 0);
}

// Status
void drivewirePrinter::drivewire_status()
{
}

void drivewirePrinter::set_printer_type(drivewirePrinter::printer_type printer_type)
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
drivewirePrinter::drivewirePrinter(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
    set_printer_type(print_type);
}

void drivewirePrinter::shutdown()
{
    if (_pptr != nullptr)
        _pptr->closeOutput();
}
/* Returns a printer type given a string model name
*/
drivewirePrinter::printer_type drivewirePrinter::match_modelname(std::string model_name)
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

#endif /* BUILD_COCO */