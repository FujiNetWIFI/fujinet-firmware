#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>
#include <string.h>
#include <FS.h>
#include <SPIFFS.h>

#include "sio.h"

#define EOL 155
#define BACKSLASH 92
#define LEFTPAREN 40
#define RIGHTPAREN 41
#define UPARROW 0xAD
#define DOWNARROW 0xAF
#define LEFTARROW 0xAC
#define RIGHTARROW 0xAE
#define BUFN 40

#define PLAIN 0
#define UNDERSCORE 0x0100
#define SYMBOL 0x0200
#define BOLD 0x0400
#define EMPHASIS 0x0800

const byte intlchar[32] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 27, UPARROW, DOWNARROW, LEFTARROW, RIGHTARROW};

enum printer_t
{
    A820,
    A822,
    A825,
    A1020,
    A1025,
    A1027,
    EMX80
};

enum paper_t
{
    RAW,
    TRIM,
    ASCII,
    PDF,
    SVG
};

class sioPrinter : public sioDevice
{
protected:
    // SIO THINGS
    byte buffer[40];
    void sio_write();
    void sio_status();
    void sio_process();
    byte lastAux1 = 0;

    // PRINTER THINGS

    File _file;
    FS* _FS;
    paper_t paperType = RAW;
    virtual void writeBuffer(byte *B, int n) = 0;

public:
    virtual void initPrinter(FS *filesystem);
    virtual void setPaper(paper_t ty) = 0;
    virtual void pageEject(){};
    virtual void flushOutput();
    size_t getOutputSize() {
        return _file.size();
    }
    int readFromOutput() {
        return _file.read();
    }
    void resetOutput();
    paper_t getPaperType();
};

class filePrinter : public sioPrinter
{
protected:
    void writeBuffer(byte *B, int n);

public:
    void setPaper(paper_t ty);
    void initPrinter(FS *filesystem);
};

class pdfPrinter : public sioPrinter
{
protected:
    // PDF THINGS
    double pageWidth = 612.0;
    double pageHeight = 792.0;
    double leftMargin = 18.0;
    double bottomMargin = 0;
    double printWidth = 576.0; // 8 inches
    double lineHeight = 12.0;
    double charWidth = 7.2;
    uint fontNumber = 1;
    uint fontSize = 12; // default 12 pica, 10 cpi
   // double fontHorizontalScaling = 100;
    double pdf_X = 0; // across the page - columns in pts
    bool BOLflag = true;
    double pdf_Y = 0; // down the page - lines in pts
    bool TOPflag = true;
    bool textMode = true; 
    int pageObjects[256];
    int pdf_pageCounter = 0;
    size_t objLocations[256]; // reference table storage
    int pdf_objCtr = 0;       // count the objects

    virtual void pdf_handle_char(byte c) = 0;
    virtual void pdf_fonts() = 0;
    void pdf_header();
    void pdf_xref();
    void pdf_begin_text(double Y);
    void pdf_new_page();
    void pdf_end_page();
    //void pdf_set_font();
    void pdf_new_line();
    void pdf_end_line();
    void pdf_add(std::string output);

    size_t idx_stream_length; // file location of stream length indictor
    size_t idx_stream_start;  // file location of start of stream
    size_t idx_stream_stop;   // file location of end of stream

    // PRINTER THINGS
    void writeBuffer(byte *B, int n);

public:
    void pageEject();
    void setPaper(paper_t ty){};
    void flushOutput();
};

class asciiPrinter : public pdfPrinter
{
protected:
    virtual void pdf_fonts();
    virtual void pdf_handle_char(byte c);

public:
    void initPrinter(FS *filesystem);
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

    void pdf_fonts();
    void pdf_handle_char(byte c);  // need a custom one to handle sideways printing

public:
    void initPrinter(FS *filesystem);
};

class atari822 : public pdfPrinter
{
protected:
    void pdf_fonts();
    void pdf_handle_char(byte c);  // need a custom one to handle sideways printing

    int gfxNumber=0;
    
public:
    void initPrinter(FS *filesystem);
};


class atari1020 : public sioPrinter
{
protected:
    bool textFlag = true;
    void svg_header();

public:
    void initPrinter(FS *filesystem);
    void setPaper(paper_t ty){};
};

#endif // guard