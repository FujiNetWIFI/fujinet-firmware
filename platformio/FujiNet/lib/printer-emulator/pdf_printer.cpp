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

    // font dictionary
    for (int i = 0; i < MAXFONTS; i++)
    {
#ifdef DEBUG
        Debug_printf("font %d - ", i + 1);
#endif
        if (fontUsed[i])
        {
            /*
            std::string subtype;
            std::string basefont;
            float width;
            float ascent;
            float capheight;
            float descent;
            byte flags;
            float bbox[4];
            float stemv;
            float xheight;
            byte ffn;
            float widths;
            std::string ffname;
        */
#ifdef DEBUG
            Debug_printf("%s; ", fonts[i]->basefont.c_str());
#endif
            pdf_objCtr++; // = 4;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d 0 obj\n<</Type/Font", pdf_objCtr);     // 4
            _file.printf("/Subtype/%s", fonts[i]->subtype.c_str()); //Type1
            _file.printf("/Name/F%d", i + 1);
            _file.printf("/BaseFont/%s", fonts[i]->basefont.c_str()); //PrestigeEliteStd
            _file.printf("/Encoding/WinAnsiEncoding");
            _file.printf("/FontDescriptor %d 0 R", pdf_objCtr + 1);                  // 5
            _file.printf("/FirstChar 0/LastChar 255/Widths %d 0 R", pdf_objCtr + 2); // 6
            _file.printf(">>\nendobj\n");
            pdf_objCtr++; // = 5;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d 0 obj\n<</Type/FontDescriptor", pdf_objCtr); // 5
            _file.printf("/FontName/%s", fonts[i]->basefont.c_str());     //PrestigeEliteStd
            _file.printf("/Flags 33/ItalicAngle 0");                      // 33 for fixed width for now todo: change for proportional when needed
            _file.printf("/Ascent %g", fonts[i]->ascent);                 // 656
            _file.printf("/Descent %g", fonts[i]->descent);               // -334
            _file.printf("/CapHeight %g", fonts[i]->capheight);           // 662
            _file.printf("/XHeight %g", fonts[i]->xheight);               // 420
            _file.printf("/StemV %g", fonts[i]->stemv);                   // 87
            _file.printf("/FontBBox[ ");
            for (int j = 0; j < 4; j++)
                _file.printf("%g ", fonts[i]->bbox[j]);
            _file.printf("]");                             // -20 -288 620 837
            _file.printf("/FontFile%d ", fonts[i]->ffnum); // 3
            _file.printf("%d 0 R", pdf_objCtr + 2);        // 7
            _file.printf(">>\nendobj\n");
            pdf_objCtr++; // = 6;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d 0 obj\n[", pdf_objCtr); // 6
            for (int j = 0; j < 256; j++)
            {
                _file.printf(" %d", fonts[i]->width[0]); // 600
                if ((j - 31) % 32 == 0)
                    _file.printf("\n");
            }
            _file.printf(" ]\nendobj\n");

            pdf_objCtr++; // = 7;
            objLocations[pdf_objCtr] = _file.position();
            _file.printf("%d 0 obj\n", pdf_objCtr); // 7
            // insert fontfile stream
            File fff = SPIFFS.open(fonts[i]->ffname.c_str(), "r"); //"/a1027font"
            while (fff.available())
            {
                _file.write(fff.read());
            }
            fff.close();
            _file.printf("\nendobj\n");
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
            if (!BOLflag && ((c == EOL) || (pdf_X > (printWidth - charWidth))))
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
    // to do: close the text string array if !BOLflag
    if (!TOPflag || pdf_pageCounter == 0)
        pdf_end_page();

    pdf_font_resource();
    pdf_add_fonts();
    pdf_page_resource();
    pdf_xref();
    printer_emu::pageEject();
    // _file.flush();
    // _file.seek(0);
}

/* 
void asciiPrinter::initPrinter(FS *filesystem)
{
    _FS = filesystem;
    resetOutput();
    //_file = f;
    // paperType = PDF;

    pageWidth = 612.0;
    pageHeight = 792.0;
    leftMargin = 18.0;
    bottomMargin = 0;
    printWidth = 576.0; // 8 inches
    lineHeight = 12.0;
    charWidth = 7.2;
    fontNumber = 1;
    fontSize = 12;

    pdf_header();
    pdf_fonts();
}

void asciiPrinter::pdf_fonts()
{
    // 3rd object: font catalog
    pdf_objCtr = 3;
    objLocations[pdf_objCtr] = _file.position();
    _file.printf("3 0 obj\n<</Font << /F1 4 0 R >>>>\nendobj\n");

    // 1027 standard font
    pdf_objCtr = 4;
    objLocations[pdf_objCtr] = _file.position();
    _file.printf("4 0 obj\n<</Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding>>\nendobj\n");
}

void asciiPrinter::pdf_handle_char(byte c)
{
    // simple ASCII printer
    if (c > 31 && c < 127)
    {
        if (c == '\\' || c == '(' || c == ')')
            _file.write('\\');
        _file.write(c);

        pdf_X += charWidth; // update x position
    }
}
 */