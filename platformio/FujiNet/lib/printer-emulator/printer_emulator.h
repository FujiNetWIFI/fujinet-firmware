#ifndef PRINTER_EMU_H
#define PRINTER_EMU_H
#include <Arduino.h>

#include <FS.h>

//#define EOL 155

enum paper_t
{
    RAW,
    TRIM,
    ASCII,
    PDF,
    SVG
};

class printer_emu
{
protected:
    FS *_FS;
    File _file;
    byte buffer[40];
    paper_t paperType;

public:
    printer_emu(paper_t ty = RAW) : paperType(ty){};
    void copyChar(byte c, byte n);
    virtual void initPrinter(FS *filesystem) = 0;
    virtual void pageEject() = 0;
    virtual bool process(byte n) = 0;

    paper_t getPaperType() { return paperType; };

    //File *getFilePtr() { return _file; }
    // virtual void flushOutput(); // do this in pageEject
    size_t getOutputSize();
    int readFromOutput();
    int readFromOutput(uint8_t *buf, size_t size);
    void resetOutput();
    void resetPrinter() { initPrinter(_FS); };
};

// close flush output file
// void sioPrinter::flushOutput()
// {
//     _file.flush();
//     _file.seek(0);
// }

#endif