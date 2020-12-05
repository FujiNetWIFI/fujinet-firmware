#ifndef XDM121_H
#define XDM121_H

#include "epson_80.h"

class xdm121 : public epson80
{
protected:
    // intl array copied from atari_1027.h - maybe better to make it a static const array of printer.h (the SIO device?)
    const uint8_t intlchar[27] = {225, 249, 209, 201, 231, 244, 242, 236, 163, 239, 252, 228, 214, 250, 243, 246, 220, 226, 251, 238, 233, 232, 241, 234, 229, 224, 197};
    bool intlFlag = false;
    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Atari XDM121"; };
};

#endif