#include "pdf_printer.h"
#include "../../include/debug.h"

pdfPrinter::~pdfPrinter()
{
#ifdef DEBUG
    Debug_println("~pdfPrinter");
#endif
}

void pdfPrinter::pdf_header()
{
#ifdef DEBUG
    Debug_println("pdf header");
#endif
    pdf_Y = 0;
    pdf_X = 0;
    pdf_pageCounter = 0;
    _file.printf("%%PDF-1.4\n");
    // first object: catalog of pages
    pdf_objCtr = 1;
    objLocations[pdf_objCtr] = _file.position();
    _file.printf("1 0 obj\n<</Type /Catalog /Pages 2 0 R>>\nendobj\n");
    // object 2 0 R is printed by pdf_page_resource() before xref
    // object 3 0 R is printed at pdf_font_resource() before xref
    pdf_objCtr = 3; // set up counter for pdf_add_font()
}

void pdfPrinter::pdf_page_resource()
{
    objLocations[2] = _file.position(); // hard code page catalog as object #2
    _file.printf("2 0 obj\n<</Type /Pages /Kids [ ");
    for (int i = 0; i < pdf_pageCounter; i++)
    {
        _file.printf("%d 0 R ", pageObjects[i]);
    }
    _file.printf("] /Count %d>>\nendobj\n", pdf_pageCounter);
}

void pdfPrinter::pdf_font_resource()
{
    int fntCtr = 0;
    objLocations[3] = _file.position();
    // font catalog
    _file.printf("3 0 obj\n<</Font <<");
    for (int i = 0; i < MAXFONTS; i++)
    {
        if (fontUsed[i])
        {
            // font dictionaries take 4 objects
            //  font dictionary
            //  font descriptor
            //  font widths
            //  font file
            _file.printf("/F%d %d 0 R ", i + 1, pdf_objCtr + 1 + fntCtr * 4); ///F1 4 0 R /F2 8 0 R>>>>\nendobj\n
            fntCtr++;
        }
    }
    _file.printf(">>>>\nendobj\n");
}

void pdfPrinter::pdf_add_fonts() // pdfFont_t *fonts[],
{
#ifdef DEBUG
    Debug_print("pdf add fonts: ");
#endif

    // OPEN LUT FILE
    char fname[30]; // filename: /f/shortname/Fi
    sprintf(fname, "/f/%s/LUT", shortname.c_str());
    File lut = SPIFFS.open(fname);
    int maxFonts = lut.parseInt();
    
    // font dictionary
    for (int i = 0; i < maxFonts; i++)
    {
#ifdef DEBUG
        Debug_printf("font %d - ", i + 1);
#endif
        // READ LINE IN LUT FILE
        // assign fontObjPos[] matrix
        if (fontUsed[i])
        {
            size_t fontObjPos[7];
            size_t fp = 0;
            char fname[30];                                        // filename: /f/shortname/Fi
            sprintf(fname, "/f/%s/F%d", shortname.c_str(), i + 1); // e.g. /f/a820/F2
            File fff = SPIFFS.open(fname, "r");                    // Font File File - fff

            for (int j = 0; j < 7; j++)
                fontObjPos[j] = lut.parseInt();

            fff.read(); // '%'
            fp++;
            fff.read(); // 'd'
            fp++;
            pdf_objCtr++; // = 6;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d", pdf_objCtr); // 6
            while (fp < fontObjPos[0])
            {
                _file.write(fff.read());
                fp++;
            }
            fff.read(); // '%'
            fp++;
            fff.read(); // 'd'
            fp++;
            _file.printf("%d", pdf_objCtr + 1); // 7
            while (fp < fontObjPos[1])
            {
                _file.write(fff.read());
                fp++;
            }
            fff.read(); // '%'
            fp++;
            fff.read(); // 'd'
            fp++;
            _file.printf("%d", pdf_objCtr + 3); // 9
            while (fp < fontObjPos[2])
            {
                _file.write(fff.read());
                fp++;
            }
            fff.read(); // '%'
            fp++;
            fff.read(); // 'd'
            fp++;
            pdf_objCtr++; // = 7;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d", pdf_objCtr); // 7
            while (fp < fontObjPos[3])
            {
                _file.write(fff.read());
                fp++;
            }
            fff.read(); // '%'
            fp++;
            fff.read(); // 'd'
            fp++;
            _file.printf("%d", pdf_objCtr + 1); // 8
            while (fp < fontObjPos[4])
            {
                _file.write(fff.read());
                fp++;
            }
            fff.read(); // '%'
            fp++;
            fff.read(); // 'd'
            fp++;
            pdf_objCtr++; // = 8;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d", pdf_objCtr); // 8
            while (fp < fontObjPos[5])
            {
                _file.write(fff.read());
                fp++;
            }
            fff.read(); // '%'
            fp++;
            fff.read(); // 'd'
            fp++;
            pdf_objCtr++; // = 9;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d", pdf_objCtr); // 9
            // insert rest of file
            while (fp < fontObjPos[6]) //(fff.available())
            {
                _file.write(fff.read());
                fp++;
            }
            fff.close();
            _file.write('\n'); // make sure there's a seperator
        }
#ifdef DEBUG
        else
        {
            Debug_print("unused; ");
        }
#endif
    }
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
    objLocations[pdf_objCtr] = _file.position();
    _file.printf("%d 0 obj\n<</Type /Page /Parent 2 0 R /Resources 3 0 R /MediaBox [0 0 %g %g] /Contents [ ", pdf_objCtr, pageWidth, pageHeight);
    pdf_objCtr++; // increment for the contents stream object
    _file.printf("%d 0 R ", pdf_objCtr);
    _file.printf("]>>\nendobj\n");

    // open content stream
    objLocations[pdf_objCtr] = _file.position();
    _file.printf("%d 0 obj\n<</Length ", pdf_objCtr);
    idx_stream_length = _file.position();
    _file.printf("00000>>\nstream\n");
    idx_stream_start = _file.position();

    // open new text object
    pdf_begin_text(pageHeight);
}

void pdfPrinter::pdf_begin_text(float Y)
{
#ifdef DEBUG
    Debug_println("pdf begin text");
#endif
    // open new text object
    _file.printf("BT\n");
    TOPflag = false;
    _file.printf("/F%u %g Tf\n", fontNumber, fontSize);
    _file.printf("%g %g Td\n", leftMargin, Y);
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
    _file.printf("0 %g Td [(", -lineHeight);
    pdf_Y -= lineHeight; // line feed
    // pdf_X = 0;              // CR over in end line()
    BOLflag = false;
}

void pdfPrinter::pdf_end_line()
{
#ifdef DEBUG
    Debug_println("pdf end line");
#endif
    _file.printf(")]TJ\n"); // close the line
    // pdf_Y -= lineHeight; // line feed - moved to new line()
    pdf_X = 0; // CR
    BOLflag = true;
}

void pdfPrinter::pdf_end_page()
{
#ifdef DEBUG
    Debug_println("pdf end page");
#endif
    // close text object & stream
    if (!BOLflag)
        pdf_end_line();
    _file.printf("ET\n");
    idx_stream_stop = _file.position();
    _file.printf("endstream\nendobj\n");
    size_t idx_temp = _file.position();
    _file.flush();
    _file.seek(idx_stream_length);
    _file.printf("%5u", (idx_stream_stop - idx_stream_start));
    _file.flush();
    _file.seek(idx_temp);
    // set counters
    pdf_pageCounter++;
    TOPflag = true;
}

void pdfPrinter::pdf_xref()
{
#ifdef DEBUG
    Debug_println("pdf xref");
#endif
    size_t xref = _file.position();
    pdf_objCtr++;
    _file.printf("xref\n");
    _file.printf("0 %u\n", pdf_objCtr);
    _file.printf("0000000000 65535 f\n");
    for (int i = 1; i < pdf_objCtr; i++)
    {
        _file.printf("%010u 00000 n\n", objLocations[i]);
    }
    _file.printf("trailer <</Size %u/Root 1 0 R>>\n", pdf_objCtr);
    _file.printf("startxref\n");
    _file.printf("%u\n", xref);
    _file.printf("%%%%EOF\n");
}

bool pdfPrinter::process(byte n)
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
    */
    int i = 0;
    byte c;

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
        // #ifdef DEBUG
        //         Debug_print(c, HEX);
        // #endif

        if (!textMode)
        {
            //this->
            pdf_handle_char(c);
        }
        else
        {
            if (BOLflag && c == EOL)
                pdf_new_line();

            // check for EOL or if at end of line and need automatic CR
            if (!BOLflag && ((c == EOL) || (pdf_X > (printWidth - charWidth +.072))))
                pdf_end_line();

            // start a new line if we need to
            if (BOLflag)
                pdf_new_line();

            // disposition the current byte
            //this->
            pdf_handle_char(c);

#ifdef DEBUG
            Debug_printf("c: %3d  x: %6.2f  y: %6.2f  ", c, pdf_X, pdf_Y);
            Debug_printf("\n");
#endif
        }

    } while (i < n && c != EOL);

    // if wrote last line, then close the page
    if (pdf_Y < bottomMargin) // lineHeight + bottomMargin
        pdf_end_page();

    return true;
}

void pdfPrinter::pageEject()
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
    printer_emu::pageEject();
}
