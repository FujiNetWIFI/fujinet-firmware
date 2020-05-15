#ifndef EPSON_80_H
#define EPSON_80_H
#include <Arduino.h>

#include "pdf_printer.h"

class epson80 : public pdfPrinter
{
protected:
    // to do: double check this against 1020 and 1027 test output and manuals
    const byte intlchar[27] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197};
    bool intlFlag = false;
    bool shortFlag = false;
    bool escMode = false;

    void pdf_handle_char(byte c);

public:
    void initPrinter(FS *filesystem);
    const char *modelname() { return "Atari 1025"; };
};

#endif