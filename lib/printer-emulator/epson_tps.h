#ifndef EPSON_TPS_H
#define EPSON_TPS_H

#include "pdf_printer.h"
#include "epson_80.h"

class epsonTPS : public epson80
{
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Epson PrintShop"; };
};

#endif