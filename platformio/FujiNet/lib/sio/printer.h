#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>
#include <string.h>
#include <FS.h>
#include <SPIFFS.h>

#include "sio.h"
#include "pdf_printer.h"
#include "atari_1027.h"
#include "file_printer.h"
#include "png_printer.h"

#define EOL 155

class sioPrinter;

class atari820 : public pdfPrinter
{
    // reverse the buffer in sioPrinter::sio_write() for sideways printing
    // the PDF standard doesn't really handle right-to-left
    // printing. The example in section 9.7 uses reverse strings.
protected:
    bool sideFlag = false;
    sioPrinter *my_sioP; // added variable to point back to sioPrinter parent

    void pdf_fonts();
    void pdf_handle_char(byte c); // need a custom one to handle sideways printing

    const pdfFont_t F1 = {
        /*
    /Type /Font
    /Subtype /Type1
    /FontDescriptor 8 0 R
    /BaseFont /Atari-820-Normal
    /FirstChar 0
    /LastChar 255
    /Widths 10 0 R
    /Encoding /WinAnsiEncoding
    /Type /FontDescriptor
    /FontName /Atari-820-Normal
    /Ascent 1000
    /CapHeight 1000
    /Descent 0
    /Flags 33
    /FontBBox [0 0 433 700]
    /ItalicAngle 0
    /StemV 87
    /XHeight 714
    /FontFile3 9 0 R
  */
        "Type1",            //F1->subtype =
        "Atari-820-Normal", //F1->basefont =
        {500},              //  F1->width[0] =
        1,                  // F1->numwidth
        1000,               // F1->ascent =
        1000,               // F1->capheight =
        0,                  // F1->descent =
        33,                 // F1->flags =
        {0, 0, 433, 700},
        87,         // F1->stemv =
        714,        // F1->xheight =
        3,          // F1->ffnum =
        "/a820norm" // F1->ffname =
    };

    const pdfFont_t F2 = {
        /*
    /Type /Font
    /Subtype /Type1
    /FontDescriptor 8 0 R
    /BaseFont /Atari-820-Sideways
    /FirstChar 0
    /LastChar 255
    /Widths 10 0 R
    /Encoding /WinAnsiEncoding
    /Type /FontDescriptor
    /FontName /Atari-820-Sideways
    /Ascent 1000
    /CapHeight 1000
    /Descent 0
    /Flags 33
    /FontBBox [0 0 600 700]
    /ItalicAngle 0
    /StemV 87
    /XHeight 1000
    /FontFile3 9 0 R
  */
        "Type1",              // F2->subtype =
        "Atari-820-Sideways", // F2->basefont =
        {666},                // F2->width[0] = ;
        1,                    // F2->numwidth =
        1000,                 // F2->ascent =
        1000,                 // F2->capheight =
        0,                    // F2->descent =
        33,                   // F2->flags =
        {0, 0, 600, 700},
        87,         // F2->stemv =
        1000,       // F2->xheight =
        3,          // F2->ffnum =
        "/a820side" // F2->ffname =
    };

public:
    atari820(sioPrinter *P) { my_sioP = P; }
    void initPrinter(FS *filesystem);
    // void setDevice(sioPrinter *P) { my_sioP = P; };
    const char *modelname() { return "Atari 820"; };
};

class atari822 : public pdfPrinter
{
protected:
    sioPrinter *my_sioP;

    void pdf_fonts();
    void pdf_handle_char(byte c); // need a custom one to handle sideways printing

    int gfxNumber = 0;

    const pdfFont_t F1 = {
        /*
      /Type /Font
      /Subtype /Type1
      /FontDescriptor 8 0 R
      /BaseFont /Atari-822-Thermal
      /FirstChar 0
      /LastChar 255
      /Widths 10 0 R
      /Encoding /WinAnsiEncoding
      /Type /FontDescriptor
      /FontName /Atari-822-Thermal
      /Ascent 1000
      /CapHeight 986
      /Descent 0
      /Flags 33
      /FontBBox [0 0 490 690]
      /ItalicAngle 0
      /StemV 87
      /XHeight 700
      /FontFile3 9 0 R
    */
        "Type1",
        "Atari-822-Thermal",
        {600},
        1,
        1000,
        986,
        0,
        33,
        {0, 0, 490, 690},
        87,
        700,
        3,
        "/a822font"};

public:
    atari822(sioPrinter *P) { my_sioP = P; }
    virtual void initPrinter(FS *filesystem);
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

    /**
     * new design idea:
     * remove pure virtual functions
     * replace with pointer to printer emulator objects
     * so printer emaulator code can be reused by non-SIO
     * applications
     * 
     * */
    printer_emu *_pptr = NULL;
    FS *_storage = NULL;

public:
    // todo: reconcile printer_type with paper_t
    enum printer_type
    {
        PRINTER_RAW = 0,
        PRINTER_ATARI_820,
        PRINTER_ATARI_822,
        PRINTER_ATARI_1027,
        PRINTER_PNG,
        PRINTER_UNKNOWN
    };

    static printer_type match_modelname(std::string modelname);
    void set_printer_type(printer_type t);
    void set_storage(FS *fs);
    void reset_printer() { set_printer_type(pt); }; // TODO: Change call in httpService to this instead of emu_printer::reset_printer()

    // Changed this to maintain a pointer in the printer object in
    // order to avoid having to send a new initPrinter every time
    // we change emulation types
    // void initPrinter(FS *fs) { _pptr->initPrinter(fs); };

    printer_emu *getPrinterPtr() { return _pptr; };

    sioPrinter();

private:
    printer_type pt;
};

extern sioPrinter sioP; // make array eventually

#endif // guard
