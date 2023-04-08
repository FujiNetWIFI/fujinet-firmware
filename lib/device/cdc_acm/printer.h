#ifndef PRINTER_H
#define PRINTER_H

#include <string>

#include "bus.h"
#include "fnFS.h"
#include "printer_emulator.h"

#define PRINTER_UNSUPPORTED "Unsupported"

class cdcPrinter : public virtualDevice
{
public:
    // todo: reconcile printer_type with paper_t
    enum printer_type
    {
        PRINTER_FILE_RAW = 0,
        PRINTER_FILE_TRIM,
        PRINTER_FILE_ASCII,
        PRINTER_ATARI_820,
        PRINTER_ATARI_822,
        PRINTER_ATARI_825,
        PRINTER_ATARI_1020,
        PRINTER_ATARI_1025,
        PRINTER_ATARI_1027,
        PRINTER_ATARI_1029,
        PRINTER_ATARI_XMM801,
        PRINTER_ATARI_XDM121,
        PRINTER_COLECO_ADAM,
        PRINTER_EPSON,
        PRINTER_EPSON_PRINTSHOP,
        PRINTER_OKIMATE10,
        PRINTER_PNG,
        PRINTER_HTML,
        PRINTER_HTML_ATASCII,
        PRINTER_INVALID
    };

    constexpr static const char * const printer_model_str[PRINTER_INVALID]
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
        "Coleco Adam Printer",
        "Epson 80",
        "Epson PrintShop",
        "Okimate 10",
        "GRANTIC",
        "HTML printer",
        "HTML ATASCII printer"
    };
    
    cdcPrinter(FileSystem *filesystem, printer_type printer_type = PRINTER_FILE_TRIM) {};
    ~cdcPrinter() {};

    static printer_type match_modelname(std::string model_name);
    void set_printer_type(printer_type printer_type) {};
    void reset_printer() {};
    time_t lastPrintTime() { return 0; };

    printer_emu *getPrinterPtr() { return nullptr; };
};

#endif /* PRINTER_H */
