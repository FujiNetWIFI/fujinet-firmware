#ifdef BUILD_IEC

#include "printer.h"

#include <cstring>

#include "../../include/debug.h"
#include "../../include/atascii.h"

#include "fnSystem.h"
#include "fnConfig.h"

#include "file_printer.h"
#include "html_printer.h"
#include "epson_80.h"
#include "epson_tps.h"
#include "okimate_10.h"
#include "commodoremps803.h"

constexpr const char *const iecPrinter::printer_model_str[PRINTER_INVALID];

iecPrinter::~iecPrinter()
{
    delete _pptr;
    _pptr = nullptr;
}

/**
 * Print from CP/M, which is one character...at...a...time...
 */
void iecPrinter::print_from_cpm(uint8_t c)
{
    // TODO IMPLEMENT
}

// Status
// TODO IMPLEMENT
/*
void iecPrinter::status()
{
}
*/

void iecPrinter::set_printer_type(iecPrinter::printer_type printer_type)
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
    case PRINTER_COMMODORE_MPS803:
        _pptr = new commodoremps803;
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
iecPrinter::iecPrinter(uint8_t devnum, FileSystem *filesystem, printer_type print_type) : IECDevice(devnum)
{
    _storage = filesystem;
    set_printer_type(print_type);
    device_active = true;
}

void iecPrinter::shutdown()
{
    if (_pptr != nullptr)
        _pptr->closeOutput();
}

/* Returns a printer type given a string model name
 */
iecPrinter::printer_type iecPrinter::match_modelname(std::string model_name)
{
    const char *models[PRINTER_INVALID] =
        {
            "Commodore MPS-803",
            "file printer (RAW)",
            "file printer (TRIM)",
            "file printer (ASCII)",
            "Epson 80",
            "Epson PrintShop",
            "Okimate 10",
            "HTML printer",
            "HTML ATASCII printer"};
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(models[i]) == 0)
            break;

    return (printer_type)i;
}


void iecPrinter::listen(uint8_t channel)
{
  _channel = channel;
}


int8_t iecPrinter::canWrite()
{
  return 1;
}


void iecPrinter::write(uint8_t data, bool eoi)
{
  _pptr->provideBuffer()[0] = data;
  _pptr->process(1, _channel, 0);
  _last_ms = fnSystem.millis();
}



#endif /* BUILD_IEC */
