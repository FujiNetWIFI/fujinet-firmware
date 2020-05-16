#ifndef EPSON_80_H
#define EPSON_80_H
#include <Arduino.h>

#include "pdf_printer.h"

class epson80 : public pdfPrinter
{
protected:
    struct epson_cmd_t
    {
        byte N;
        byte ctr;
    } epson_cmd;
    bool escMode = false;

    void pdf_handle_char(byte c);

public:
    void initPrinter(FS *filesystem);
    const char *modelname() { return "Epson 80"; };
};

#endif