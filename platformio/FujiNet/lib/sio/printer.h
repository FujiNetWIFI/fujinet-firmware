#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>
#include <string.h>
#include <FS.h>

#include "sio.h"

#define EOL 155

void pdfUpload();

class sioPrinter : public sioDevice
{
private:
    byte buffer[40];
    void sio_write();
    void sio_status() override;
    void sio_process() override;

    int pageWidth = 612;
    int pageHeight = 792;
    int leftMargin = 66;
    int bottomMargin = 2;
    int maxLines = 66;
    int maxCols = 80;
    int lineHeight = 12;
    int fontSize = 10;
    const char *fontName = "Courier";
    int pdf_lineCounter = 0;
    int pdf_offset = 0;             // used to store location offset to next object
    int objLocations[100];          // reference table storage - set >=maxLines+5
    int pdf_objCtr = 0;             // count the objects
    bool eolFlag;
    
    void pdf_header();
    void pdf_xref();
    void pdf_add_line(std::string L);
    void atari_to_c_str(byte *S);
    std::string output;
    int j;

    File *_file;

public:
    //sioDisk(){};
    //sioDisk(int devnum=0x31) : _devnum(devnum){};
    // void mount(File *f);
    // void handle();
    void initPDF(File *f);
    void formFeed();
};

#endif // guard