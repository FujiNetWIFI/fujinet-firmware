#ifndef EPSON_TPS_H
#define EPSON_TPS_H

#include "epson_80.h"

class epsonTPS : public epson80
{
protected:
    // virtual void pdf_clear_modes() override{};
    virtual void post_new_file() override
    {
        epson80::post_new_file();
        pdf_dY = lineHeight;
    }; // go up one line for The Print Shop

public:
    const char *modelname() { return "Epson PrintShop"; };
};

#endif