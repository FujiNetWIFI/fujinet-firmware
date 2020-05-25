#ifndef HTML_PRINTER_H
#define HTML_PRINTER_H

#include "printer_emulator.h"

class htmlPrinter : public printer_emu
{
private:
    bool inverse = false;

protected:
    virtual void post_new_file() override;
    virtual void pre_page_eject() override;

public:
    htmlPrinter(paper_t ty = HTML) : printer_emu{ty} {};
    virtual void initPrinter(FileSystem *fs);
    virtual void pageEject();
    virtual bool process(byte linelen, byte aux1, byte aux2);

    virtual const char * modelname() { 
        return paperType == HTML ? "HTML printer" : "HTML ATASCII printer"; };

    ~htmlPrinter();

    void setPaper(paper_t ty) { paperType = ty; };
};

#endif
