#ifdef BUILD_APPLE
#ifndef PRINTER_H
#define PRINTER_H

#include <string.h>

#include "bus.h"
#include "printer_emulator.h"
#include "fnFS.h"

#define PRINTER_UNSUPPORTED "Unsupported"

class applePrinter : public iwmDevice
{
protected:
    // TODO following are copied over from sio/printer.h
    // SMARTPORT THINGS
    // uint8_t _buffer[40];
    // void smart_write(uint8_t aux1, uint8_t aux2);
    // void smart_status() override;
    // void smart_process(uint32_t commanddata, uint8_t checksum) override;
    // void shutdown() override;

    printer_emu *_pptr = nullptr;
    FileSystem *_storage = nullptr;

    time_t _last_ms;
    // TODO following are copied over from sio/printer.h
    // uint8_t _lastaux1;
    // uint8_t _lastaux2;

public:
    enum printer_type
    {
        PRINTER_FILE_RAW = 0,
        PRINTER_FILE_TRIM,
        PRINTER_FILE_ASCII,
        PRINTER_EPSON,
        PRINTER_HTML,
        PRINTER_INVALID
    };

    constexpr static const char * const printer_model_str[PRINTER_INVALID]
    {
        "file printer (RAW)",
        "file printer (TRIM)",
        "file printer (ASCII)",
        "Epson 80",
        "HTML printer",
    };

    applePrinter(FileSystem *filesystem, printer_type printer_type = PRINTER_FILE_TRIM);
    ~applePrinter();

    static printer_type match_modelname(std::string model_name);
    void set_printer_type(printer_type printer_type);
    void reset_printer() { set_printer_type(_ptype); };
    time_t lastPrintTime() { return _last_ms; };
    // void print_from_cpm(uint8_t c); // left over from ATARI although could use it on APPLE maybe?

    printer_emu *getPrinterPtr() { return _pptr; };

private:
    printer_type _ptype;
};


#endif // guard
#endif // BUILD_APPLE
