#ifndef ATARI_1025_H
#define ATARI_1025_H
#include <Arduino.h>

#include "pdf_printer.h"

class atari1025 : public pdfPrinter
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
    
protected:
    size_t fontObjPos[3][7] = {
        {
            // F1
            // Atari-1025-Normal
            66,   // FontDescriptor Reference
            151,  // Widths Reference
            200,  // FontDescriptor Object
            420,  // FontFile Reference
            439,  // FontFile Object
            6150, // Widths Object
            7194  // fragment length
        },
        {
            // F2
            // Atari-1025-Elongated
            66,   // FontDescriptor Reference
            154,  // Widths Reference
            203,  // FontDescriptor Object
            427,  // FontFile Reference
            446,  // FontFile Object
            4797, // Widths Object
            6097  // fragment length
        },
        {
            // F3
            // Atari-1025-Condensed
            66,   // FontDescriptor Reference
            154,  // Widths Reference
            203,  // FontDescriptor Object
            426,  // FontFile Reference
            445,  // FontFile Object
            3775, // Widths Object
            4819  // fragment length
        }};


};

#endif