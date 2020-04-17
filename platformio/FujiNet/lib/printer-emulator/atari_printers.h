#include <Arduino.h>
#include "pdf_printer.h"

#include <SPIFFS.h>

#include "printer.h"

#define EOL 155

// to do: double check this against 1020 and 1027 test output and manuals
const byte intlchar[28] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 27};

class filePrinter : public printer_emu
{
public:
    virtual void initPrinter(FS *filesystem);
    virtual void pageEject(){};
    virtual bool process(const byte *buf, byte n);

    void setPaper(paper_t ty) { paperType = ty; };
};

class atari1027 : public pdfPrinter
{
protected:
    bool intlFlag = false;
    bool uscoreFlag = false;
    bool escMode = false;

    void pdf_fonts();
    void pdf_handle_char(byte c);

public:
    void initPrinter(FS *filesystem);
};

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
};

// class atari1020 : public svgPlotter
// {
// protected:
//     bool textFlag = true;
//     void svg_header();

// public:
//     void initPrinter(File *f);
//     void setPaper(paper_t ty){};
// };