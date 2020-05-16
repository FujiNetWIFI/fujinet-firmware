#ifndef EPSON_80_H
#define EPSON_80_H
#include <Arduino.h>

#include "pdf_printer.h"


class epson80 : public pdfPrinter
{
protected:
    void pdf_handle_char(byte c);

    enum state_t
    {
        TEXT,
        ESC
    };

public:
    void initPrinter(FS *filesystem);
    const char *modelname() { return "Epson 80"; };
};

#endif