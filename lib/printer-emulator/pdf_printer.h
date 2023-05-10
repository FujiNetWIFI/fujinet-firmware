#ifndef PDF_PRINTER_H
#define PDF_PRINTER_H

/* This is really a virtual class, as it's not meant to be instantiated on its own, but
 inherited from by other, full-fledged printer classes (e.g. Atari 820/822)
*/
#include <string>

#include "../../include/atascii.h"

#include "printer_emulator.h"


#define MAXFONTS 33 // maximum number of fonts can use

enum class colorMode_t
{
    off = 0,
    yellow,
    magenta,
    cyan,
    process
};

class pdfPrinter : public printer_emu
{
protected:
    // PDF THINGS
    double pageWidth;
    double pageHeight;
    double leftMargin;
    double topMargin = 0.0;
    double bottomMargin;
    double printWidth;
    double lineHeight;
    double charWidth;
    uint8_t fontNumber;
    double fontSize;
    uint8_t fontHorizScale = 100;
    std::string shortname;
    bool fontUsed[MAXFONTS] = {true}; // initialize first one to true, always use default font
    double pdf_X = 0.;                 // across the page - columns in pts
    bool BOLflag = true;
    double pdf_Y = 0.;  // down the page - lines in pts
    double pdf_dY = 0.; // used for linefeeds with pdf rise parameter
    bool TOPflag = true;
    bool textMode = true;
    colorMode_t colorMode = colorMode_t::off;

    int pageObjects[256];
    int pdf_pageCounter = 0.;
    size_t objLocations[256]; // reference table storage
    int pdf_objCtr = 0;       // count the objects

    void pdf_header();
    void pdf_add_fonts(); // pdfFont_t *fonts[],
    void pdf_new_page();
    void pdf_begin_text(double Y);
    void pdf_new_line();
    void pdf_end_line();
    void pdf_set_rise();
    void pdf_end_page();
    void pdf_page_resource();
    void pdf_font_resource();
    void pdf_xref();

    size_t idx_stream_length; // file location of stream length indictor
    size_t idx_stream_start;  // file location of start of stream
    size_t idx_stream_stop;   // file location of end of stream

    virtual void pdf_clear_modes() = 0;
    virtual void pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2) = 0;
    virtual bool process_buffer(uint8_t linelen, uint8_t aux1, uint8_t aux2) override;

    virtual void pre_close_file() override;

public:

    virtual const char *modelname(void) = 0;
    pdfPrinter() { _paper_type = PDF; };

};

#endif // guard
