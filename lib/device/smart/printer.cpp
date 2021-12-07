#include "printer.h"

#include "file_printer.h"
#include "html_printer.h"
#include "epson_80.h"

void applePrinter::set_printer_type(applePrinter::printer_type printer_type)
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


applePrinter::printer_type applePrinter::match_modelname(std::string model_name)
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
