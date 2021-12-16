#include "pdf_printer.h"
#include "../utils/utils.h"
#include "../../include/debug.h"
#include "fnFsSPIF.h"

void pdfPrinter::pdf_header()
{
#ifdef DEBUG
    Debug_println("pdf header");
#endif
    pdf_Y = 0;
    pdf_X = 0;
    pdf_pageCounter = 0;
    fprintf(_file, "%%PDF-1.4\n");
    // first object: catalog of pages
    pdf_objCtr = 1;
    objLocations[pdf_objCtr] = ftell(_file);
    fprintf(_file, "1 0 obj\n<</Type /Catalog /Pages 2 0 R>>\nendobj\n");
    // object 2 0 R is printed by pdf_page_resource() before xref
    // object 3 0 R is printed at pdf_font_resource() before xref
    pdf_objCtr = 3; // set up counter for pdf_add_font()
}

void pdfPrinter::pdf_page_resource()
{
    objLocations[2] = ftell(_file); // hard code page catalog as object #2
    fprintf(_file, "2 0 obj\n<</Type /Pages /Kids [ ");
    for (int i = 0; i < pdf_pageCounter; i++)
    {
        fprintf(_file, "%d 0 R ", pageObjects[i]);
    }
    fprintf(_file, "] /Count %d>>\nendobj\n", pdf_pageCounter);
}

void pdfPrinter::pdf_font_resource()
{
    int fntCtr = 0;
    objLocations[3] = ftell(_file);
    // font catalog
    fprintf(_file, "3 0 obj\n<</Font <<");
    for (int i = 0; i < MAXFONTS; i++)
    {
        if (fontUsed[i])
        {
            // font dictionaries take 4 objects
            //  font dictionary
            //  font descriptor
            //  font widths
            //  font file
            fprintf(_file, "/F%d %d 0 R ", i + 1, pdf_objCtr + 1 + fntCtr * 4); /// F1 4 0 R /F2 8 0 R>>>>\nendobj\n
            fntCtr++;
        }
    }
    fprintf(_file, ">>>>\nendobj\n");
}

void pdfPrinter::pdf_add_fonts() // pdfFont_t *fonts[],
{
#ifdef DEBUG
    Debug_print("pdf add fonts: ");
#endif

    // OPEN LUT FILE
    char fname[30]; // filename: /f/shortname/Fi
    sprintf(fname, "/f/%s/LUT", shortname.c_str());
    FILE *lut = fnSPIFFS.file_open(fname);
    int maxFonts = util_parseInt(lut);

    // font dictionary
    for (int i = 0; i < maxFonts; i++)
    {
#ifdef DEBUG
        Debug_printf("font %d - ", i + 1);
#endif
        // READ LINE IN LUT FILE
        size_t fontObjPos[7];
        for (int j = 0; j < 7; j++)
            fontObjPos[j] = util_parseInt(lut);

        // assign fontObjPos[] matrix
        if (fontUsed[i])
        {
            size_t fp = 0;
            char fname[30];                                        // filename: /f/shortname/Fi
            sprintf(fname, "/f/%s/F%d", shortname.c_str(), i + 1); // e.g. /f/a820/F2
            FILE *fff = fnSPIFFS.file_open(fname);                 // Font File File - fff

            fgetc(fff); // '%'
            fp++;
            fgetc(fff); // 'd'
            fp++;
            pdf_objCtr++; // = 6;
            objLocations[pdf_objCtr] = ftell(_file);
            fprintf(_file, "%d", pdf_objCtr); // 6
            while (fp < fontObjPos[0])
            {
                fputc(fgetc(fff), _file);
                fp++;
            }
            fgetc(fff); // '%'
            fp++;
            fgetc(fff); // 'd'
            fp++;
            fprintf(_file, "%d", pdf_objCtr + 1); // 7
            while (fp < fontObjPos[1])
            {
                fputc(fgetc(fff), _file);
                fp++;
            }
            fgetc(fff); // '%'
            fp++;
            fgetc(fff); // 'd'
            fp++;
            fprintf(_file, "%d", pdf_objCtr + 3); // 9
            while (fp < fontObjPos[2])
            {
                fputc(fgetc(fff), _file);
                fp++;
            }
            fgetc(fff); // '%'
            fp++;
            fgetc(fff); // 'd'
            fp++;
            pdf_objCtr++; // = 7;
            objLocations[pdf_objCtr] = ftell(_file);
            fprintf(_file, "%d", pdf_objCtr); // 7
            while (fp < fontObjPos[3])
            {
                fputc(fgetc(fff), _file);
                fp++;
            }
            fgetc(fff); // '%'
            fp++;
            fgetc(fff); // 'd'
            fp++;
            fprintf(_file, "%d", pdf_objCtr + 1); // 8
            while (fp < fontObjPos[4])
            {
                fputc(fgetc(fff), _file);
                fp++;
            }
            fgetc(fff); // '%'
            fp++;
            fgetc(fff); // 'd'
            fp++;
            pdf_objCtr++; // = 8;
            objLocations[pdf_objCtr] = ftell(_file);
            fprintf(_file, "%d", pdf_objCtr); // 8
            while (fp < fontObjPos[5])
            {
                fputc(fgetc(fff), _file);
                fp++;
            }
            fgetc(fff); // '%'
            fp++;
            fgetc(fff); // 'd'
            fp++;
            pdf_objCtr++; // = 9;
            objLocations[pdf_objCtr] = ftell(_file);
            fprintf(_file, "%d", pdf_objCtr); // 9
            // insert rest of file
            while (fp < fontObjPos[6]) //(fff.available())
            {
                fputc(fgetc(fff), _file);
                fp++;
            }
            fclose(fff);
            fputc('\n', _file); // make sure there's a seperator
        }
#ifdef DEBUG
        else
        {
            Debug_print("unused; ");
        }
#endif
    }

    fclose(lut);
#ifdef DEBUG
    Debug_println("done.");
#endif
}

void pdfPrinter::pdf_new_page()
{ // open a new page
#ifdef DEBUG
    Debug_println("pdf new page");
#endif
    pdf_objCtr++;
    pageObjects[pdf_pageCounter] = pdf_objCtr;
    objLocations[pdf_objCtr] = ftell(_file);
    fprintf(_file, "%d 0 obj\n<</Type /Page /Parent 2 0 R /Resources 3 0 R /MediaBox [0 0 %g %g] /Contents [ ", pdf_objCtr, pageWidth, pageHeight);
    pdf_objCtr++; // increment for the contents stream object
    fprintf(_file, "%d 0 R ", pdf_objCtr);
    fprintf(_file, "]>>\nendobj\n");

    // open content stream
    objLocations[pdf_objCtr] = ftell(_file);
    fprintf(_file, "%d 0 obj\n<</Length ", pdf_objCtr);
    idx_stream_length = ftell(_file);
    fprintf(_file, "0000000000 >>\nstream\n");
    idx_stream_start = ftell(_file);

    // open new text object
    pdf_begin_text(pageHeight - topMargin);
}

void pdfPrinter::pdf_begin_text(double Y)
{
#ifdef DEBUG
    Debug_println("pdf begin text");
#endif
    // open new text object
    fprintf(_file, "BT\n");
    TOPflag = false;
    fprintf(_file, "/F%u %g Tf %d Tz\n", fontNumber, fontSize, fontHorizScale);
    fprintf(_file, "%g %g Td\n", leftMargin, Y);
    pdf_Y = Y; // reset print roller to top of page
    pdf_X = 0; // set carriage to LHS
    BOLflag = true;
}

void pdfPrinter::pdf_new_line()
{
#ifdef DEBUG
    Debug_println("pdf new line");
#endif

    // position new line and start text string array
    if (pdf_dY != 0)
        fprintf(_file, "0 Ts ");
    pdf_dY -= lineHeight;
    fprintf(_file, "0 %g Td [(", pdf_dY);
    pdf_Y += pdf_dY; // line feed
    pdf_dY = 0;
    // pdf_X = 0;              // CR over in end line()
    BOLflag = false;
}

void pdfPrinter::pdf_end_line()
{
#ifdef DEBUG
    Debug_println("pdf end line");
#endif
    fprintf(_file, ")]TJ\n"); // close the line
    // pdf_Y -= lineHeight; // line feed - moved to new line()
    pdf_X = 0; // CR
    BOLflag = true;
    // clear any one-line modes
    pdf_clear_modes();
}

void pdfPrinter::pdf_set_rise()
{
    fprintf(_file, ")]TJ %g Ts [(", pdf_dY);
}

void pdfPrinter::pdf_end_page()
{
#ifdef DEBUG
    Debug_println("pdf end page");
#endif
    // close text object & stream
    if (!BOLflag)
        pdf_end_line();
    fprintf(_file, "ET\n");
    idx_stream_stop = ftell(_file);
    fprintf(_file, "endstream\nendobj\n");
    size_t idx_temp = ftell(_file);
    fflush(_file);
    fseek(_file, idx_stream_length, SEEK_SET);
    fprintf(_file, "%10u", (idx_stream_stop - idx_stream_start));
    fflush(_file);
    fseek(_file, idx_temp, SEEK_SET);
    // set counters
    pdf_pageCounter++;
    TOPflag = true;
}

void pdfPrinter::pdf_xref()
{
#ifdef DEBUG
    Debug_println("pdf xref");
#endif
    size_t xref = ftell(_file);
    pdf_objCtr++;
    fprintf(_file, "xref\n");
    fprintf(_file, "0 %u\n", pdf_objCtr);
    fprintf(_file, "0000000000 65535 f\n");
    for (int i = 1; i < pdf_objCtr; i++)
    {
        fprintf(_file, "%010u 00000 n\n", objLocations[i]);
    }
    fprintf(_file, "trailer <</Size %u/Root 1 0 R>>\n", pdf_objCtr);
    fprintf(_file, "startxref\n");
    fprintf(_file, "%u\n", xref);
    fprintf(_file, "%%%%EOF\n");
}

bool pdfPrinter::process_buffer(uint8_t n, uint8_t aux1, uint8_t aux2)
{
    /**
     * idea for 850-connected printers. The 850 translates EOL to CR.
     * could make EOL = either 155 or 13. So if its an SIO printer,
     * we keep EOL = 155. If its an 850 printer, we convert 155 to 13
     * and then check for 13's on newline, etc. This would allow 13's
     * to pass through and be executed.
     *
     * Then there's the problem of the printer either having auto LF or not.
     * The 825 has auto LF unless the hardware is modified.
     * Epson FX80 comes default with no auto LF, but I suspect an Atari
     * user would make it auto linefeed.
     *
     * Could add an "autolinefeed" flag to the pdf-printer class.
     * Could add an EOL-CR conversion flag in the pdf-printer class to have
     * 850 emulation.
     *
     * How to make a CR command and a LF command?
     *  CR is easy - just a " 0 0 Td " pdf command
     *  LF with a CR is what we're already doing - e.g., " 0 -18 Td "
     *  LF by itself is probably necessary - 2 cases
     *      1 - when the carriage is at 0, then this is just like a LFCR
     *      2 - in the middle of the line, we need to start printing at the
     *          current pdf_X value. However, the pdf commands aren't well
     *          suited for this. A pdf "pdf_X -18 Td" would do the trick, but
     *          it then sets the relative 0,0 coordinate to the middle of the
     *          line and a CR won't take it back like it does now. Would need
     *          to remember that we were in the middle of a line and adjust
     *          with a "-last_X 0 Td" for the next CR. Another option is to do
     *          absolute positioning of each and every line, which I think requires
     *          each line to be it's own text object and is what i was
     *          doing originally. Not my first choice.
     *      3 - I think this: when at start of line, do #1
     *          but when in middle of line use the pdf rendering variable called
     *          "rise" to adjust the baseline of printing. Make a variable to
     *          keep track of rise, which is an offset from the baseline.
     *          Could set rise back to zero on a CR by adding the rise to the
     *          lineHeight in pdf_new_line(). Explicitly set rise to 0 on
     *          CR/EOL when rise/=0. simply put "0 Ts" in the stream.
     *
     */
    int i = 0;
    uint8_t c;
    uint8_t cc;

#ifdef DEBUG
    Debug_printf("Processing %d chars\n", n);
#endif

    // algorithm for graphics:
    // if textMode, then can do the regular stuff
    // if !textMode, then don't deal with BOL, EOL.
    // check for TOP always just in case.
    // can graphics mode ignore over-flowing a page for now?
    // textMode is set inside of pdf_handle_char at first character, so...
    // need to test for textMode inside the loop

    if (TOPflag)
        pdf_new_page();

    // loop through string
    do
    {
        c = buffer[i++];
        cc = c;
        if (translate850 && c == ATASCII_EOL)
            c = ASCII_CR; // the 850 interface converts EOL to CR

        // #ifdef DEBUG
        //         Debug_print(c, HEX);
        // #endif

        if (!textMode || static_cast<bool>(colorMode))
        {
            pdf_handle_char(c, aux1, aux2);
        }
        else
        {
            // Temporarily bypass eol handling if required.
            // The real fix is to split CR/LF handling.
            if (_eol_bypass == false)
            {
                if (BOLflag && c == _eol)
                    pdf_new_line();

                // check for EOL or if at end of line and need automatic CR
                if (!BOLflag && ((c == _eol) || (pdf_X > (printWidth - charWidth + .072))))
                    pdf_end_line();

                // start a new line if we need to
                if (BOLflag)
                    pdf_new_line();
            }

            // disposition the current byte
            pdf_handle_char(c, aux1, aux2);

#ifdef DEBUG
            Debug_printf("c: %3d  x: %6.2f  y: %6.2f  ", c, pdf_X, pdf_Y + pdf_dY);
            Debug_printf("\n");
#endif
        }

    } while (i < n && (cc != ATASCII_EOL));

    // if wrote last line, then close the page
    if (pdf_Y < bottomMargin) // lineHeight + bottomMargin
        pdf_end_page();

    return true;
}

void pdfPrinter::pre_close_file()
{
    if (TOPflag && pdf_pageCounter == 0)
        pdf_new_page(); // make a blank page
    if (!BOLflag)
        pdf_end_line(); // close out the current line of text
    if (!TOPflag || pdf_pageCounter == 0)
        pdf_end_page();

    pdf_font_resource();
    pdf_add_fonts();
    pdf_page_resource();
    pdf_xref();

    // printer_emu::pageEject();
}
