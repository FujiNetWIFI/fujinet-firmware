#ifndef PRINTER_EMU_H
#define PRINTER_EMU_H
#include <Arduino.h>

#include <FS.h>

#define EOL 155

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
    File *_file;
    paper_t paperType;

public:
    virtual void initPrinter(File *f) = 0;
    virtual void pageEject() = 0;
    virtual bool process(const byte* buf, byte n) = 0;
    paper_t getPaperType() { return paperType; };
    File *getFilePtr() { return _file; }
};




#endif