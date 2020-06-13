#ifndef PRINTER_H
#define PRINTER_H

#include <string.h>

#include "sio.h"
#include "printer_emulator.h"
#include "fnFS.h"

class sioPrinter : public sioDevice
{
protected:
    // SIO THINGS
    byte _buffer[40];
    void sio_write(byte aux1, byte aux2);
    void sio_status() override;
    void sio_process() override;

    printer_emu *_pptr = nullptr;
    FileSystem *_storage = nullptr;

    time_t _last_ms;
    byte _lastaux1;
    byte _lastaux2;

public:
    // todo: reconcile printer_type with paper_t
    enum printer_type
    {
        PRINTER_FILE_RAW = 0,
        PRINTER_FILE_TRIM,
        PRINTER_FILE_ASCII,
        PRINTER_ATARI_820,
        PRINTER_ATARI_822,
        PRINTER_ATARI_1020,
        PRINTER_ATARI_1025,
        PRINTER_ATARI_1027,
        PRINTER_ATARI_1029,
        PRINTER_EPSON,
        PRINTER_PNG,
        PRINTER_HTML,
        PRINTER_HTML_ATASCII,
        PRINTER_INVALID
    };
    
    sioPrinter(FileSystem *filesystem, printer_type printer_type = PRINTER_FILE_TRIM);
    ~sioPrinter();

    static printer_type match_modelname(std::string model_name);
    void set_printer_type(printer_type printer_type);
    void reset_printer() { set_printer_type(_ptype); };
    time_t lastPrintTime() { return _last_ms; };

    printer_emu *getPrinterPtr() { return _pptr; };


private:
    printer_type _ptype;
};

#endif // guard
