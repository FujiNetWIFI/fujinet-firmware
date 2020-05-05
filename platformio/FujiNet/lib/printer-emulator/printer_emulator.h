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
    SVG,
    PNG,
    HTML
};

class printer_emu
{
protected:
    FS *_FS;
    File _file;
    byte buffer[40];
    paper_t paperType;

    // executed after a new printer output file is created
    virtual void post_new_file() {};
    // executed before a printer output file is closed and prepared for reading
    virtual void pre_page_eject() {};

public:
    printer_emu(paper_t ty = RAW) : paperType(ty){};
    // Destructor must be virtual to allow for proper cleanup of derived classes
    virtual ~printer_emu() = 0;

    void copyChar(byte c, byte n);
    virtual void initPrinter(FS *filesystem);
    virtual void pageEject() = 0;
    virtual bool process(byte n) = 0;

    paper_t getPaperType() { return paperType; };

    virtual const char *modelname() = 0;
    //File *getFilePtr() { return _file; }
    // virtual void flushOutput(); // do this in pageEject
    size_t getOutputSize();
    int readFromOutput();
    int readFromOutput(uint8_t *buf, size_t size);
    void resetOutput();
    //void resetPrinter() { initPrinter(_FS); };

};

// close flush output file
// void sioPrinter::flushOutput()
// {
//     _file.flush();
//     _file.seek(0);
// }

#endif