#ifndef XMM801_H
#define XMM801_H

#include "epson_80.h"

class xmm801 : public epson80
{
protected:
    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;

public:
    const char *modelname() { return "Atari XMM801"; };
};

#endif