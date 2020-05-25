#ifndef FILE_PRINTER_H
#define FILE_PRINTER_H
//#include <Arduino.h>

#include "printer_emulator.h"

class filePrinter : public printer_emu
{
public:
    filePrinter(FileSystem *fs, paper_t ty = TRIM) : printer_emu{fs, ty} {};
    virtual void initPrinter();
    virtual void pageEject();
    virtual bool process(byte linelen, byte aux1, byte aux2);

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
