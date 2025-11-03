#ifdef BUILD_IEC
#ifndef IEC_PRINTER_H
#define IEC_PRINTER_H

#include <string>


#include "../../bus/bus.h"
#include "../../bus/iec/IECDevice.h"
#include "../printer-emulator/printer_emulator.h"

#include "fnFS.h"


#define PRINTER_UNSUPPORTED "Unsupported"

class iecPrinter : public IECDevice
{
protected:
    printer_emu *_pptr = nullptr;
    FileSystem *_storage = nullptr;
    time_t _last_ms;

    void shutdown();

public:
    // todo: reconcile printer_type with paper_t
    enum printer_type
    {
        PRINTER_COMMODORE_MPS803 = 0,
        PRINTER_FILE_RAW,
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
        PRINTER_EPSON,
        PRINTER_EPSON_PRINTSHOP,
        PRINTER_OKIMATE10,
        PRINTER_PNG,
        PRINTER_HTML,
        PRINTER_HTML_ATASCII,
        PRINTER_INVALID
    };

public:
    constexpr static const char * const printer_model_str[PRINTER_INVALID]
    {
        "Commodore MPS-803",
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
        "Epson 80",
        "Epson PrintShop",
        "Okimate 10",
        "GRANTIC",
        "HTML printer",
        "HTML ATASCII printer"
    };


    iecPrinter(uint8_t devnum, FileSystem *filesystem, printer_type printer_type = PRINTER_FILE_TRIM);
    ~iecPrinter();

    static printer_type match_modelname(std::string model_name);
    void set_printer_type(printer_type printer_type);
    void reset_printer() { set_printer_type(_ptype); };
    time_t lastPrintTime() { return _last_ms; };
    void print_from_cpm(uint8_t c);

    printer_emu *getPrinterPtr() { return _pptr; };

    // overriding the IECDevice isActive() function because device_active
    // must be a global variable
    bool device_active = true;
    virtual bool isActive() { return device_active; }

private:
  // called before a write() call to determine whether the device
  // is ready to receive data.
  // canWrite() is allowed to take an indefinite amount of time
  // canWrite() should return:
  //  <0 if more time is needed before data can be accepted (call again later), blocks IEC bus
  //   0 if no data can be accepted (error)
  //  >0 if at least one uint8_t of data can be accepted
  virtual int8_t canWrite();

  // called when the device received data
  // write() will only be called if the last call to canWrite() returned >0
  // write() must return within 1 millisecond
  // the "eoi" parameter will be "true" if sender signaled that this is the last
  // data uint8_t of a transmission
  virtual void write(uint8_t data, bool eoi);

  // called when bus master sends LISTEN command
  // listen() must return within 1 millisecond
  virtual void listen(uint8_t channel);

  uint8_t _channel;
  printer_type _ptype;
};


#endif // IEC_PRINTER_H
#endif // BUILD_IEC
