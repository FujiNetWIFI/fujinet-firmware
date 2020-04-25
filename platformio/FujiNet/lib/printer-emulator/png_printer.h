#ifndef PNG_PRINTER_H
#define PNG_PRINTER_H
#include <Arduino.h>

#include "printer_emulator.h"

class pngPrinter : public printer_emu
{
public:
    pngPrinter(paper_t ty = PNG) : printer_emu{ty} {};
    virtual void initPrinter(FS *filesystem);
    virtual void pageEject();
    virtual bool process(byte n);
};

#endif
