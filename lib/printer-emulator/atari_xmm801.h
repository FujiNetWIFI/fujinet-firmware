#ifndef XMM801_H
#define XMM801_H

#include "epson_80.h"

class xmm801 : public epson80
{
protected:
    // intl array copied from atari_1025.h - maybe better to make it a static const array of printer.h (the SIO device?)
    const uint8_t intlchar[32] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197, 0, 0xa0 + 28, 0xa0 + 29, 0xa0 + 30, 0xa0 + 31};
    bool intlFlag = false;
    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;

public:
    const char *modelname() { return "Atari XMM801"; };
};

#endif