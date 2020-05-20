#ifndef PDF_PRINTER_H
#define PDF_PRINTER_H

#include <Arduino.h>

#include <string>
//#include <FS.h>
#include <SPIFFS.h>

#include "printer_emulator.h"
#include "../../include/atascii.h"

#define MAXFONTS 6 // maximum number of fonts can use

class pdfPrinter : public printer_emu
{
protected:
  // ATARI THINGS
  bool translate850 = false; // default to sio printer
  byte _eol = ATASCII_EOL;   // default to atascii eol

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
  std::string shortname;
  bool fontUsed[MAXFONTS] = {true}; // initialize first one to true, always use default font
  float pdf_X = 0.;                 // across the page - columns in pts
  bool BOLflag = true;
  float pdf_Y = 0.;  // down the page - lines in pts
  float pdf_dY = 0.; // used for linefeeds with pdf rise parameter
  bool TOPflag = true;
  bool textMode = true;
  int pageObjects[256];
  int pdf_pageCounter = 0.;
  size_t objLocations[256]; // reference table storage
  int pdf_objCtr = 0;       // count the objects

  virtual void pdf_handle_char(byte c) = 0;

  void pdf_header();
  void pdf_add_fonts(); // pdfFont_t *fonts[],
  void pdf_new_page();
  void pdf_begin_text(float Y);
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

public:
  pdfPrinter(paper_t ty = PDF) : printer_emu{ty} {};
  virtual void pageEject();
  virtual bool process(byte n);

  virtual const char *modelname() { return "PDF printer"; };
  ~pdfPrinter();
};

#endif // guard