#ifndef EPSON_TPS_H
#define EPSON_TPS_H

#include "epson_80.h"

class epsonTPS : public epson80

{
protected:
    virtual void pdf_clear_modes() override{};
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Epson PrintShop"; };
};

#endif