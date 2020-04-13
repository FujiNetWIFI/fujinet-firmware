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
    FS* _FS;
    File *_file;
    paper_t paperType;

public:
    virtual void initPrinter(File *f) = 0;
    virtual void pageEject() = 0;
    virtual bool process(const byte* buf, byte n) = 0;
    paper_t getPaperType() { return paperType; };
    
    File *getFilePtr() { return _file; }
        virtual void flushOutput();
    size_t getOutputSize() {
        return _file.size();
    }
    int readFromOutput() {
        return _file.read();
    }
    int readFromOutput(uint8_t *buf, size_t size) {
        return _file.read(buf, size);
    }
    void resetOutput();
    paper_t getPaperType();

};




#endif