#ifndef ATARI_1027_H
#define ATARI_1027_H

#include "pdf_printer.h"

class atari1027 : public pdfPrinter
{
protected:
    const uint8_t intlchar[27] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197};

    bool intlFlag = false;
    bool uscoreFlag = false;
    bool escMode = false;
    
   virtual void pdf_clear_modes() override {};
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2);
    virtual void post_new_file() override;
public:
    const char *modelname() { return "Atari 1027"; };
};

#endif