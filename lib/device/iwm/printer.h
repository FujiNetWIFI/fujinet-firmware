#ifdef BUILD_APPLE
#ifndef PRINTER_H
#define PRINTER_H

#include <string.h>

#include "../printer-emulator/printer_emulator.h"
#include "fnFS.h"

#include "../../bus/bus.h"

#define PRINTER_UNSUPPORTED "Unsupported"

class iwmPrinter : public virtualDevice
{
protected:

    // IWM Status methods
    iwm_device_info_block_t create_dib_reply_packet() override;
    iwm_device_status_block_t create_status_reply_packet() override;

    // IWM methods
    void iwm_open(const iwm_decoded_cmd_t &cmd) override;
    void iwm_close(const iwm_decoded_cmd_t &cmd) override;
    void iwm_write(const iwm_decoded_cmd_t &cmd) override;
    void shutdown() override {}

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
