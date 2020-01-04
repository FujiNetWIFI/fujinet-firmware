#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>
#include <string.h>
#include <FS.h>

#include "sio.h"

#define EOL 155
#define BACKSLASH 0x5c
#define LEFTPAREN 0x28
#define RIGHTPAREN 0x29
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
    int bottomMargin = 2;
    int maxLines = 66;
    int maxCols = 80;
    int lineHeight = 12;
    int fontSize = 12;
    const char *fontName = "Courier";
    int pdf_lineCounter = 0;
    int pdf_offset = 0;    // used to store location offset to next object
    int objLocations[100]; // reference table storage - set >=maxLines+5
    int pdf_objCtr = 0;    // count the objects
    bool eolFlag;

    void processBuffer(byte *B, int n);

    void pdf_header();
    void pdf_xref();
    void pdf_add_line(std::string L);
    std::string buffer_to_string(byte *S);
    std::string output;
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