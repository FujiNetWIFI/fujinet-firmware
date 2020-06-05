#ifndef ATARI_1025_H
#define ATARI_1025_H

#include "pdf_printer.h"

class atari1025 : public pdfPrinter
{
protected:
    // to do: double check this against 1020 and 1027 test output and manuals
    const uint8_t intlchar[27] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197};
    bool intlFlag = false;
    bool shortFlag = false;
    bool escMode = false;
    
   virtual void pdf_clear_modes() override {};
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Atari 1025"; };
};

#endif
