#include "pdf_printer.h"

#include <SPIFFS.h>

#include "printer.h"

// #define BACKSLASH 92
// #define LEFTPAREN 40
// #define RIGHTPAREN 41
// #define UPARROW 0xAD
// #define DOWNARROW 0xAF
// #define LEFTARROW 0xAC
// #define RIGHTARROW 0xAE
// #define BUFN 40

// #define PLAIN 0
// #define UNDERSCORE 0x0100
// #define SYMBOL 0x0200
// #define BOLD 0x0400
// #define EMPHASIS 0x0800

// enum printer_t
// {
//     A820,
//     A822,
//     A825,
//     A1020,
//     A1025,
//     A1027,
//     EMX80
// };

// to do: double check this against 1020 and 1027 test output and manuals
const byte intlchar[28] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 27};

class filePrinter : public printer_emu
{
public:
    virtual void initPrinter(File *f);
    virtual void pageEject(){};
    virtual bool process(const byte* buf, byte n);

    void setPaper(paper_t ty) {paperType = ty;};
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
    void initPrinter(File *f);
};

class atari820 : public pdfPrinter
{
    // reverse the buffer in sioPrinter::sio_write() for sideways printing
    // the PDF standard doesn't really handle right-to-left
    // printing. The example in section 9.7 uses reverse strings.

protected:
    bool sideFlag = false;

    void pdf_fonts();
    void pdf_handle_char(byte c); // need a custom one to handle sideways printing

public:
    void initPrinter(File *f);
};

class atari822 : public pdfPrinter
{
protected:
    void pdf_fonts();
    void pdf_handle_char(byte c); // need a custom one to handle sideways printing

    int gfxNumber = 0;

public:
    void initPrinter(File *f);
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