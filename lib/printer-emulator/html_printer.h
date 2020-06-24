#ifndef HTML_PRINTER_H
#define HTML_PRINTER_H

#include "printer_emulator.h"

class htmlPrinter : public printer_emu
{
private:
    bool inverse = false;

protected:
    virtual void post_new_file() override;
    virtual void pre_close_file() override;
    virtual bool process_buffer(uint8_t linelen, uint8_t aux1, uint8_t aux2);

public:
    htmlPrinter(paper_t ptype=HTML) { _paper_type = ptype; };

    virtual const char * modelname() { 
        return _paper_type == HTML ? "HTML printer" : "HTML ATASCII printer"; };
};

#endif
