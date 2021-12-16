#ifndef COLECO_PRINTER_H
#define COLECO_PRINTER_H

#include "pdf_printer.h"

class colecoprinter : public pdfPrinter
{
protected:

   virtual void pdf_clear_modes() override {};
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2);
    virtual void post_new_file() override;
public:
    const char *modelname() { return "Coleco Adam Printer"; };
};

#endif