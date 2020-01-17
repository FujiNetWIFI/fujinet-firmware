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
#define BUFN 40

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
    PDF
};

class sioPrinter : public sioDevice
{
private:
    byte buffer[40];
    void sio_write();
    void sio_status() override;
    void sio_process() override;

    paper_t paperType = PDF;
    int pageWidth = 612;
    int pageHeight = 792;
    int leftMargin = 18;
    int bottomMargin = 6;
    int maxLines = 66;
    int maxCols = 80;
    int lineHeight = 12;
    int fontSize = 12;
    const char *fontName = "Courier";
    int pdf_lineCounter = 0;
    int voffset;
    int pageObjects[256];
    int pdf_pageCounter = 0;
    size_t objLocations[256]; // reference table storage
    int pdf_objCtr = 0;    // count the objects
    bool eolFlag = false;
    bool intlFlag = false;
    bool uscoreFlag = false;
    bool escMode = false;

    void processBuffer(byte *B, int n);

    void pdf_header();
    void pdf_xref();
    void pdf_new_page();
    void pdf_end_page();
    void pdf_begin_text(int font, int fsize, int vpos);
    void pdf_end_text();
    void pdf_add_line(std::u16string L);
    size_t idx_stream_length; // file location of stream length indictor
    size_t idx_stream_start; // file location of start of stream
    size_t idx_stream_stop; // file location of end of stream
    std::u16string buffer_to_string(byte *S);
    std::u16string output;
    int j;

    File *_file;

public:
    //sioDisk(){};
    //sioDisk(int devnum=0x31) : _devnum(devnum){};
    // void mount(File *f);
    // void handle();
    void initPrinter(File *f, paper_t ty);
    void initPrinter(File *f);
    void pageEject();
};

#endif // guard