#ifndef PRINTER_H
#define PRINTER_H
//#include <Arduino.h>
#include <string.h>
//#include <FS.h>
//#include <SPIFFS.h>

#include "fnFsSD.h"
#include "fnFsSPIF.h"

#include "sio.h"
#include "pdf_printer.h"
#include "printer_emulator.h"

//#define EOL 155

class sioPrinter;

class atari820 : public pdfPrinter
{
    // reverse the buffer in sioPrinter::sio_write() for sideways printing
    // the PDF standard doesn't really handle right-to-left
    // printing. The example in section 9.7 uses reverse strings.
protected:
    bool sideFlag = false;
    sioPrinter *my_sioP; // added variable to point back to sioPrinter parent

    void pdf_handle_char(byte c); // need a custom one to handle sideways printing

public:
    atari820(sioPrinter *P) { my_sioP = P; }
    void initPrinter(FileSystem *fs);
    // void setDevice(sioPrinter *P) { my_sioP = P; };
    const char *modelname() { return "Atari 820"; };
};

class atari822 : public pdfPrinter
{
protected:
    sioPrinter *my_sioP;

    void pdf_handle_char(byte c); // need a custom one to handle sideways printing

    int gfxNumber = 0;

public:
    atari822(sioPrinter *P) { my_sioP = P; }
    virtual void initPrinter(FileSystem *fs);
    const char *modelname() { return "Atari 822"; };

    //void setDevice(sioPrinter *P) { my_sioP = P; };
};

class sioPrinter : public sioDevice
{
    friend atari820;
    friend atari822;

protected:
    // SIO THINGS
    byte buffer[40];
    void sio_write();
    void sio_status();
    void sio_process();
    byte lastAux1 = 0;

    printer_emu *_pptr = nullptr;
    FileSystem *_storage = nullptr;

    time_t last_ms;

public:
    // todo: reconcile printer_type with paper_t
    enum printer_type
    {
        PRINTER_FILE_RAW = 0,
        PRINTER_FILE_TRIM,
        PRINTER_FILE_ASCII,
        PRINTER_ATARI_820,
        PRINTER_ATARI_822,
        PRINTER_ATARI_1025,
        PRINTER_ATARI_1027,
        PRINTER_PNG,
        PRINTER_HTML,
        PRINTER_HTML_ATASCII,
        PRINTER_INVALID
    };

    static printer_type match_modelname(std::string modelname);
    void set_printer_type(printer_type t);
    void set_storage(FileSystem *fs);
    void reset_printer() { set_printer_type(pt); };
    time_t lastPrintTime() { return last_ms; };

    printer_emu *getPrinterPtr() { return _pptr; };

    sioPrinter();

private:
    printer_type pt;
};

extern sioPrinter sioP; // make array eventually

#endif // guard
