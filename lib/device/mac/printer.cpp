#ifdef BUILD_MAC
#include "printer.h"

#include "file_printer.h"
#include "html_printer.h"
#include "epson_80.h"
#include "fnSystem.h"

constexpr const char *const macPrinter::printer_model_str[PRINTER_INVALID];

macPrinter::macPrinter(FileSystem *filesystem, printer_type printer_type)
{
    _storage = filesystem;
    set_printer_type(printer_type);
}

macPrinter::~macPrinter()
{
    delete _pptr;
    _pptr = nullptr;
}

/**
 * Print from CP/M, which is one character...at...a...time...
 */
void macPrinter::print_from_cpm(uint8_t c)
{
    _pptr->provideBuffer()[_llen++]=c;

    if (c == 0x0D || c == 0x0a || _llen == 80)
    {
        _last_ms = fnSystem.millis();
        _pptr->process(_llen, 0, 0);
        _llen = 0;
    }
}

void macPrinter::process(mac_cmd_t cmd)
{
}

void macPrinter::set_printer_type(macPrinter::printer_type printer_type)
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
    case PRINTER_EPSON:
        _pptr = new epson80;
        break;
    case PRINTER_HTML:
        _pptr = new htmlPrinter;
        break;
    default:
        _pptr = new filePrinter;
        _ptype = PRINTER_FILE_RAW;
        break;
    }

    _pptr->initPrinter(_storage);
}

macPrinter::printer_type macPrinter::match_modelname(std::string model_name)
{
    const char *models[PRINTER_INVALID] =
        {
            "file printer (RAW)",
            "file printer (TRIM)",
            "file printer (ASCII)",
            "Epson 80",
            "HTML printer"};
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(models[i]) == 0)
            break;

    return (printer_type)i;
}
#endif /* BUILD_APPLE */