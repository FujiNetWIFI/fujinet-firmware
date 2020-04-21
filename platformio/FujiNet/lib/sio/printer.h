#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>
#include <string.h>
#include <FS.h>
#include <SPIFFS.h>

#include "sio.h"
#include "pdf_printer.h"

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

public:
    void initPrinter(FS *filesystem);
    void setDevice(sioPrinter *P) { my_sioP = P; };

};

class atari822 : public pdfPrinter
{
protected:
    sioPrinter *my_sioP;

    void pdf_fonts();
    void pdf_handle_char(byte c); // need a custom one to handle sideways printing

    int gfxNumber = 0;

public:
    virtual void initPrinter(FS *filesystem);
    void setDevice(sioPrinter *P) { my_sioP = P; };

protected:
    pdfFont_t F1 = {
        //pdfFont_t F1;
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
};

class sioPrinter : public sioDevice
{
    friend atari820;
    friend atari822;

protected:
    // does not work: friend void atari820::pdf_handle_char(byte c);
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
    printer_emu *_pptr;

public:
    void connect_printer(printer_emu *P) { _pptr = P; };
    printer_emu *getPrinterPtr() { return _pptr; };
    void initPrinter(FS *fs) { _pptr->initPrinter(fs); };
};

extern sioPrinter sioP; // make array eventually

#endif // guard