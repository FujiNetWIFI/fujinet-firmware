#include "html_printer.h"
#include "../../include/atascii.h"
#include "../../include/debug.h"


// TODO: Combine html_printer.cpp/h and file_printer.cpp/h

int AtasciiToStandardUnicode[128] = 
{
    9829, 9500, 9621, 9496, 9508, 9488, 9585, 9586,   //   8
    9698, 9623, 9699, 9629, 9624, 9620, 9601, 9622,   //  16
    9827, 9484, 9472, 9532, 11044, 9604, 9615, 9516,  //  24
    9524, 9612, 9492, 8455, 8593, 8595, 8592, 8594,   //  32
    0, 0, 0, 0, 0, 0, 38, 0,                          //  40
    0, 0, 0, 0, 0, 0, 0, 0,                           //  48
    0, 0, 0, 0, 0, 0, 0, 0,                           //  56
    0, 0, 0, 0, 60, 0, 62, 0,                         //  64
    0, 0, 0, 0, 0, 0, 0, 0,                           //  72
    0, 0, 0, 0, 0, 0, 0, 0,                           //  80
    0, 0, 0, 0, 0, 0, 0, 0,                           //  88
    0, 0, 0, 0, 0, 0, 0, 0,                           //  96
    9830, 0, 0, 0, 0, 0, 0, 0,                        // 104
    0, 0, 0, 0, 0, 0, 0, 0,                           // 112
    0, 0, 0, 0, 0, 0, 0, 0,                           // 120
    0, 0, 0, 9824, 9475, 8598, 9658, 9668             // 128
};

bool htmlPrinter::process_buffer(uint8_t n, uint8_t aux1, uint8_t aux2)
{
    int i = 0;
    
    const char *inv_on = "<span class=\"iv\">";
    const char *inv_off = "</span>";
    const char *escape = "&#x%x;";

    while (i < n)
    {
        uint8_t c = buffer[i];

        // ATASCII_EOL is a line break and also a record break
        // (so ignore the rest of the buffer)
        if (c == ATASCII_EOL)
        {
            if(inverse) {
                inverse = false;
                fputs(inv_off, _file);
            }
            fputs(ASCII_CRLF, _file);
            break;
        }

        // Handle differently depending on wether we're doing this with an Atari font
        if(_paper_type == HTML_ATASCII)
        {
            // If it's a printable character, just copy it
            if(c >=32 && c <= 122 && c != 96)
            {
                // One more check to make sure it's not one of the HTML characters we have to escape
                if(AtasciiToStandardUnicode[c] != 0)
                    fprintf(_file, escape, AtasciiToStandardUnicode[c]);
                else
                    fputc(c,_file);
            }
            // It's an ATASCII character, so map it into the Private Use Area of the Atari font
            // PUA starts at 0xE000
            else
            {
                fprintf(_file, escape, 0xE000 + c);
            }

        }
        else
        {
            // If this is an inverse character, turn on inverse mode if it
            // isn't already on and convert the character to non-inverse
            if(c >= 128)
            {
                if(false == inverse)
                {
                    inverse = true;
                    fputs(inv_on, _file);
                }
                c -= 128;
            }
            // If it wasn't an inverse character and inverse was on, turn it off
            else if(inverse)
            {
                inverse = false;
                fputs(inv_off,  _file);
            }

            // If we have a replacement HTML entitiy for this character, use that
            if(AtasciiToStandardUnicode[c] != 0)
            {
                fprintf(_file, escape, AtasciiToStandardUnicode[c]);
            }
            // Otherwise just write the character
            else 
            {
                fputc(c, _file);
            }
        }

        i++;
    }
    return true;
}

void htmlPrinter::pre_close_file()
{
    const char *htm = "</html>";
    fputs(htm, _file);
    inverse = false;
}

void htmlPrinter::post_new_file()
{
    const char *htm1 = "<html><head><meta charset=\"utf-8\"/>\r\n<style>";
    const char *css = "body{font-family:\"Courier New\",Courier,monospace;white-space:pre-wrap;} .iv{color:white;background-color:black;}\r\n";
    const char *htm2 = "</style></head>\r\n";

    fputs(htm1, _file);

    // Load CSS that embeds the Atari font (or don't)
    if(_paper_type == HTML_ATASCII)
        copy_file_to_output("/atarifont.css");
    else
        fputs(css, _file);

    fputs(htm2, _file);
}
