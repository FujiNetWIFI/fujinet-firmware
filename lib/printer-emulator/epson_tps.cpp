#include "epson_tps.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

void epsonTPS::post_new_file()
{
    // translate850 = true;
    // _eol = ASCII_CR;

    // shortname = "epson";

    // pageWidth = 612.0;
    // pageHeight = 792.0;
    // // leftMargin = 18.0;
    // // bottomMargin = 0;
    // // printWidth = 576.0; // 8 inches
    // // lineHeight = 12.0;
    // // charWidth = 7.2;
    // // fontNumber = 1;
    // // fontSize = 12;
    // at_reset(); // moved all those parameters so could be excuted with ESC-@ command

    // pdf_header();
    // escMode = false;

    epson80::post_new_file();
    pdf_dY = lineHeight; // go up one line for The Print Shop

}
