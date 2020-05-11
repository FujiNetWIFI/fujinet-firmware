#ifndef ATARI_1027_H
#define ATARI_1027_H
#include <Arduino.h>

#include "pdf_printer.h"

class atari1027 : public pdfPrinter
{
protected:
    const byte intlchar[27] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197};

    bool intlFlag = false;
    bool uscoreFlag = false;
    bool escMode = false;

    void pdf_handle_char(byte c);

public:
    void initPrinter(FS *filesystem);
    const char *modelname() { return "Atari 1027"; };

protected:
    const size_t fontObjPos[1][7] = {
        {
            // F1
            // PrestigeEliteStd
            66,    // FontDescriptor Reference
            150,   // Widths Reference
            199,   // FontDescriptor Object
            424,   // FontFile Reference
            443,   // FontFile Object
            21694, // Widths Object
            22738  // fragment length
        }};
};

#endif