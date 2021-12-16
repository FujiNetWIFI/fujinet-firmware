#ifndef ADAM_PRINTER_H
#define ADAM_PRINTER_H

#include <string.h>

#include "bus.h"
#include "../printer-emulator/printer_emulator.h"
#include "fnFS.h"

class adamPrinter : public adamNetDevice
{
protected:
    // SIO THINGS
    uint8_t _buffer[40];
    void sio_write(uint8_t aux1, uint8_t aux2);
    
    virtual void adamnet_control_status();
    virtual void adamnet_control_send();
    virtual void adamnet_control_ready();

    void adamnet_process(uint8_t b) override;
    void shutdown() override;

    printer_emu *_pptr = nullptr;
    FileSystem *_storage = nullptr;

    time_t _last_ms;
    uint8_t _lastaux1;
    uint8_t _lastaux2;

public:
    // todo: reconcile printer_type with paper_t
    enum printer_type
    {
        PRINTER_FILE_RAW = 0,
        PRINTER_FILE_TRIM,
        PRINTER_FILE_ASCII,
        PRINTER_COLECO_ADAM,
        PRINTER_ATARI_820,
        PRINTER_ATARI_822,
        PRINTER_ATARI_825,
        PRINTER_ATARI_1020,
        PRINTER_ATARI_1025,
        PRINTER_ATARI_1027,
        PRINTER_ATARI_1029,
        PRINTER_ATARI_XMM801,
        PRINTER_ATARI_XDM121,
        PRINTER_EPSON,
        PRINTER_EPSON_PRINTSHOP,
        PRINTER_OKIMATE10,
        PRINTER_PNG,
        PRINTER_HTML,
        PRINTER_HTML_ATASCII,
        PRINTER_INVALID
    };

    adamPrinter(FileSystem *filesystem, printer_type printer_type = PRINTER_FILE_TRIM);
    ~adamPrinter();

    static printer_type match_modelname(std::string model_name);
    void set_printer_type(printer_type printer_type);
    void reset_printer() { set_printer_type(_ptype); };
    time_t lastPrintTime() { return _last_ms; };

    printer_emu *getPrinterPtr() { return _pptr; };


private:
    printer_type _ptype;
    TaskHandle_t ioTask = NULL;
    bool _backwards = false;

};

#endif /* ADAM_PRINTER_H */
