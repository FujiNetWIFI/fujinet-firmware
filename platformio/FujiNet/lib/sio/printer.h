#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>
#include <string.h>
#include <FS.h>

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
    void sio_status() override;
    void sio_process() override;
    byte lastAux1 = 0;

    // PDF THINGS

    paper_t paperType = PDF;
    double pageWidth = 612.0;
    double pageHeight = 792.0;
    double leftMargin = 18.0;
    double bottomMargin = 0;
    double maxWidth = 576.0; // 8 inches
    double lineHeight = 12.0;
    double charWidth = 7.2;
    double pdf_X = 0; // across the page - columns in pts
    bool BOLflag = true;
    double pdf_Y = 0; // down the page - lines in pts
    bool TOPflag = true;
    int pageObjects[256];
    int pdf_pageCounter = 0;
    size_t objLocations[256]; // reference table storage
    int pdf_objCtr = 0;       // count the objects

    void pdf_header();
    void pdf_fonts();
    void pdf_xref();
    void pdf_new_page();
    void pdf_end_page();
    void pdf_set_font();
    void pdf_new_line();
    void pdf_end_line();
    void pdf_handle_char(byte c);
    void pdf_add(std::string output);
    size_t idx_stream_length; // file location of stream length indictor
    size_t idx_stream_start;  // file location of start of stream
    size_t idx_stream_stop;   // file location of end of stream

    // SVG THINGS

    void svg_add(std::string S);

    // PRINTER THINGS

    void writeBuffer(byte *B, int n);
    File *_file;

public:
    void initPrinter(File *f, paper_t ty);
    void initPrinter(File *f);
    void pageEject();
    paper_t getPaperType();
};

class atari1027 : public sioPrinter
{
protected:
    bool intlFlag = false;
    bool uscoreFlag = false;
    bool escMode = false;
    void pdf_handle_char(byte c);

public:
    void initPrinter(File *f, paper_t ty);
};

class atari820 : public sioPrinter
{
protected:
    paper_t paperType = PDF;
    double pageWidth = 612.0;
    double pageHeight = 792.0;
    double leftMargin = 18.0;
    double bottomMargin = 0;
    double maxWidth = 576.0; // 8 inches
    double lineHeight = 12.0;
    double charWidth = 7.2;

    void pdf_handle_char(byte c);
};

#endif // guard