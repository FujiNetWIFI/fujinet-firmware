#include <Arduino.h>

#include <string>
//#include <FS.h>
#include <SPIFFS.h>

#include "printer_emulator.h"

#define EOL 155 // pdf printer library does use Atari EOL instead of /r/n

struct pdfFont_t
{
/* 
  7 0 obj
  << 
    /Type /Font
    /Subtype /Type1
    /FontDescriptor 8 0 R
    /BaseFont /Atari-820-Normal
    /FirstChar 0
    /LastChar 255
    /Widths 10 0 R
    /Encoding /WinAnsiEncoding
  >>
  endobj 
  8 0 obj
  << 
    /Type /FontDescriptor
    /FontName /Atari-820-Normal
    /Ascent 1000
    /CapHeight 1000
    /Descent 0
    /Flags 33
    /FontBBox [0 0 433 700]
    /ItalicAngle 0
    /StemV 87
    /XHeight 714
    /FontFile3 9 0 R
  >>
  endobj
*/
  std::string subtype;
  std::string basefont;
  uint16_t width[256]; // uniform spacing for now, todo: proportional
  uint16_t numwidth;
  float ascent;
  float capheight;
  float descent;
  byte flags;
  float bbox[4];
  float stemv;
  float xheight;
  byte ffnum;
  std::string ffname;
};

class pdfPrinter : public printer_emu
{
protected:
  // PDF THINGS
  float pageWidth;
  float pageHeight;
  float leftMargin;
  float bottomMargin;
  float printWidth;
  float lineHeight;
  float charWidth;
  byte fontNumber;
  float fontSize;

  pdfFont_t* fontary[6];

  float pdf_X = 0.; // across the page - columns in pts
  bool BOLflag = true;
  float pdf_Y = 0.; // down the page - lines in pts
  bool TOPflag = true;
  bool textMode = true;
  int pageObjects[256];
  int pdf_pageCounter = 0.;
  size_t objLocations[256]; // reference table storage
  int pdf_objCtr = 0;       // count the objects

  virtual void pdf_handle_char(byte c) = 0;

  void pdf_header();
  void pdf_add_fonts(pdfFont_t *fonts[], int n);
  void pdf_new_page();
  void pdf_begin_text(float Y);
  void pdf_new_line();
  void pdf_end_line();
  void pdf_end_page();
  void pdf_xref();

  size_t idx_stream_length; // file location of stream length indictor
  size_t idx_stream_start;  // file location of start of stream
  size_t idx_stream_stop;   // file location of end of stream

public:
  //virtual void initPrinter(FS *filesystem) = 0;
  virtual void pageEject();
  virtual bool process(const byte *buf, byte n);

};

class asciiPrinter : public pdfPrinter
{
protected:
  virtual void pdf_handle_char(byte c);
  virtual void pdf_fonts();

public:
  virtual void initPrinter(FS *filesystem);
};
