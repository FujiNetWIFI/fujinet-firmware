#ifndef ATARI_1027_H
#define ATARI_1027_H
#include <Arduino.h>

#include "pdf_printer.h"

// to do: double check this against 1020 and 1027 test output and manuals
const byte intlchar[28] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 27};

class atari1027 : public pdfPrinter
{
protected:
    bool intlFlag = false;
    bool uscoreFlag = false;
    bool escMode = false;

    void pdf_fonts();
    void pdf_handle_char(byte c);

public:
    void initPrinter(FS *filesystem);

    const char * modelname() { return "Atari 1027"; };

protected:
    /*
        7 0 obj
        << 
        /Type /Font
        /Subtype /Type1
        /FontDescriptor 8 0 R
        /BaseFont /PrestigeEliteStd
        /FirstChar 0
        /LastChar 255
        /Widths 10 0 R
        /Encoding /WinAnsiEncoding
        >>
        endobj
        8 0 obj
        << 
        /Type /FontDescriptor
        /FontName /PrestigeEliteStd
        /Ascent 656
        /CapHeight 612
        /Descent -344
        /Flags 33
        /FontBBox [-20 -288 620 837]
        /ItalicAngle 0
        /StemV 87
        /XHeight 420
        /FontFile3 9 0 R
    */

    //   std::string subtype;
    //   std::string basefont;
    //   float width; // uniform spacing for now, todo: proportional
    //   float ascent;
    //   float capheight;
    //   float descent;
    //   byte flags;
    //   float bbox[4];
    //   float stemv;
    //   float xheight;
    //   byte ffnum;
    //   std::string ffname;

    pdfFont_t F1 = {
        "Type1",               // F1.subtype =
        "PrestigeEliteStd",    // F1.basefont =
        {600},                 // F1.width =
        1,                     //numwidth
        656,                   // F1.ascent=
        612,                   // F1.capheight=
        -344,                  // F1.descent=
        33,                    // F1.flags =
        {-20, -288, 620, 837}, // bbox
        87,                    // stemv
        420,                   // F1.xheight=
        3,                     //F1.xheight=
        "/a1027font"           // F1.ffname =
    };
};

#endif