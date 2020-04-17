#ifndef FILE_PRINTER_H
#define FILE_PRINTER_H
#include <Arduino.h>

#include "printer_emulator.h"

class filePrinter : public printer_emu
{
public:
    virtual void initPrinter(FS *filesystem);
    virtual void pageEject(){};
    virtual bool process(const byte *buf, byte n);

    void setPaper(paper_t ty) { paperType = ty; };
};

#endif
