#ifndef FILE_PRINTER_H
#define FILE_PRINTER_H
//#include <Arduino.h>

#include "printer_emulator.h"

class filePrinter : public printer_emu
{
    virtual bool process_buffer(byte linelen, byte aux1, byte aux2);
    virtual void post_new_file() {};
    virtual void pre_close_file() {};

public:
    filePrinter(paper_t ptype=RAW) { _paper_type = ptype; };

    virtual const char * modelname() 
    {
        if(_paper_type == ASCII)
            return "file printer (ASCII)";
        else if (_paper_type == RAW)
            return "file printer (RAW)";
        else
            return "file printer (TRIM)";
    };
};

#endif
