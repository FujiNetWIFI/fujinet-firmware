#include "html_printer.h"
#include "../../include/atascii.h"
#include "../../include/debug.h"

void htmlPrinter::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);
}

htmlPrinter::~htmlPrinter()
{
#ifdef DEBUG
    Debug_println("~htmlPrinter");
#endif
}

bool htmlPrinter::process(byte n)
{
    int i = 0;
    
    static bool inverse = false;

    const char *inv_on = "<span class=\"inv\">";
    const char *inv_off = "</span>";

    while (i < n)
    {
        if (buffer[i] == ATASCII_EOL)
        {
            // ATASCII_EOL is a line break and also a record break
            // (so ignore the rest of the buffer)
            if(inverse) {
                inverse = false;
                _file.write((const uint8_t *)inv_off, strlen(inv_off));
            }
            _file.printf("\r\n");
            break;
        } else if (buffer[i] > 31 && buffer[i] < 123)
        {
            if(inverse) {
                inverse = false;
                _file.write((const uint8_t *)inv_off, strlen(inv_off));
            }
            _file.write(buffer[i]);

        } else if (buffer[i] > (31+128) && buffer[i] < (123+128))
        {
            if(false == inverse) {
                inverse = true;
                _file.write((const uint8_t *)inv_on, strlen(inv_on));
            }
            _file.write(buffer[i]-128);
        }
        i++;
    }
    return true;
}

void htmlPrinter::pre_page_eject()
{
    const char *htm = "</pre></html>";
    _file.write((const uint8_t *)htm, strlen(htm));
}

void htmlPrinter::post_new_file()
{
    const char *htm = "<html><head><style>.inv{color:white;background-color:black;}</style></head><pre>";
    _file.write((const uint8_t *)htm, strlen(htm));
}

void htmlPrinter::pageEject()
{
    printer_emu::pageEject();
}
