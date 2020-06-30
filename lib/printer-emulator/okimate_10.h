#ifndef OKIMATE_10_H
#define OKIMATE_10_H

#include "atari_1025.h"

class okimate10 : public atari1025
{
protected:
    bool colorMode = false;
    bool gfxMode = false;
    bool compressedMode = false;
    uint8_t okiState = 0; // need state var mask instead of Mode flags?

    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Okimate 10"; };
};

#endif
