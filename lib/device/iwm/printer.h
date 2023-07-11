#ifdef BUILD_APPLE
#ifndef PRINTER_H
#define PRINTER_H

#include <string.h>

#include "../printer-emulator/printer_emulator.h"
#include "fnFS.h"

#include "../../bus/bus.h"

#define PRINTER_UNSUPPORTED "Unsupported"

class iwmPrinter : public iwmDevice
{
protected:

    // IWM Status methods
    void send_status_reply_packet() override;
    void send_extended_status_reply_packet() override;
    void send_status_dib_reply_packet() override;
    void send_extended_status_dib_reply_packet() override;

    // IWM methods
    void iwm_status(iwm_decoded_cmd_t cmd) override;
    void iwm_open(iwm_decoded_cmd_t cmd) override;
    void iwm_close(iwm_decoded_cmd_t cmd) override;
    void iwm_write(iwm_decoded_cmd_t cmd) override;
    void process(iwm_decoded_cmd_t cmd) override;
    void shutdown() {}

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

    iwmPrinter(FileSystem *filesystem, printer_type printer_type = PRINTER_FILE_TRIM);
    ~iwmPrinter();

    static printer_type match_modelname(std::string model_name);
    void set_printer_type(printer_type printer_type);
    void reset_printer() { set_printer_type(_ptype); };
    time_t lastPrintTime() { return _last_ms; };
    // void print_from_cpm(uint8_t c); // left over from ATARI although could use it on APPLE maybe?

    printer_emu *getPrinterPtr() { return _pptr; };
    void print_from_cpm(uint8_t c);

private:
    printer_type _ptype;
    int _llen = 0;
};


#endif // guard
#endif // BUILD_APPLE
