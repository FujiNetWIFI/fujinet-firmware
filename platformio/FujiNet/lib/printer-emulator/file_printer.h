#ifndef FILE_PRINTER_H
#define FILE_PRINTER_H
//#include <Arduino.h>

#include "printer_emulator.h"

class filePrinter : public printer_emu
{
public:
    filePrinter(paper_t ty = TRIM) : printer_emu{ty} {};
    virtual void initPrinter(FS *filesystem);
    virtual void pageEject();
    virtual bool process(byte n);

    virtual const char * modelname() 
    {
        if(paperType == ASCII)
            return "file printer (ASCII)";
        else if (paperType == RAW)
            return "file printer (RAW)";
        else
            return "file printer (TRIM)";
    };

    ~filePrinter();

    void setPaper(paper_t ty) { paperType = ty; };
};

#endif
