#ifdef BUILD_CBM

#include "../../include/atascii.h"
#include "printer.h"

#include "file_printer.h"
#include "html_printer.h"
#include "atari_820.h"
#include "atari_822.h"
#include "atari_825.h"
#include "svg_plotter.h"
#include "atari_1025.h"
#include "atari_1027.h"
#include "atari_1029.h"
#include "epson_80.h"
#include "epson_tps.h"
#include "atari_xmm801.h"
#include "atari_xdm121.h"
#include "okimate_10.h"
#include "png_printer.h"

iecPrinter::~iecPrinter()
{
    delete _pptr;
}

iecPrinter::printer_type iecPrinter::match_modelname(std::string model_name)
{
    const char *models[PRINTER_INVALID] =
        {
            "file printer (RAW)",
            "file printer (TRIM)",
            "file printer (ASCII)",
            "ADAM Printer",
            "Epson 80",
            "Epson PrintShop",
            "HTML printer",
            "HTML ATASCII printer"};
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(models[i]) == 0)
            break;

    return (printer_type)i;
}

void iecPrinter::adamnet_process(uint8_t b)
{
    
}

void iecPrinter::shutdown()
{

}

#endif /* BUILD_CBM */