#include "file_printer.h"
#include "../../include/debug.h"
#include "../../include/atascii.h"

// TODO: Combine html_printer.cpp/h and file_printer.cpp/h

void filePrinter::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);
}

filePrinter::~filePrinter()
{
#ifdef DEBUG
    Debug_println("~filePrinter");
#endif
}

bool filePrinter::process(byte n)
{
    int i;

    switch (paperType)
    {
    // Entire record contents are written, even data after the ATASCII_EOL
    case RAW:
        for (i = 0; i < n; i++)
            _file.write(buffer[i]);
        break;
    // Everything up to and including the ATASCII_EOL is written without modification
    case TRIM:
        for (i = 0; i < n; i++)
         {
            _file.write(buffer[i]);
            if (buffer[i] == ATASCII_EOL)
                break;
         }
        break;
    case ASCII:
    default:
        // Only ASCII-valid characters (including inverse charaters stripped of inverse bit)
        // are written up to the ATASCII_EOL which is converted to ASCII_CRLF
        for (i = 0; i < n; i++)
         {
            if (buffer[i] == ATASCII_EOL)
            {
                _file.print(ASCII_CRLF);
                break;
            }
            // If it's an inverse character, convert to normal
            char c = ATASCII_REMOVE_INVERSE(buffer[i]);
            // If it's a printable character, just copy it
            if(c >=32 && c <= 122 && c != 96)            
            {
                _file.write(c);
            }
         }
    }
    return true;
}

void filePrinter::pageEject()
{
    printer_emu::pageEject();
}
