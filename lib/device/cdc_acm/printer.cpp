#ifdef BUILD_CDC

#include "printer.h"

cdcPrinter::printer_type cdcPrinter::match_modelname(std::string model_name)
{
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(cdcPrinter::printer_model_str[i]) == 0)
            break;

    return (printer_type)i;
}

#endif /* BUILD_CDC */