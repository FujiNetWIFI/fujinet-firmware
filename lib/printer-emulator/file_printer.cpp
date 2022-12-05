#include "file_printer.h"

#include "../../include/debug.h"
#include "../../include/atascii.h"

// TODO: Combine html_printer.cpp/h and file_printer.cpp/h


bool filePrinter::process_buffer(uint8_t n, uint8_t aux1, uint8_t aux2)
{
    int i;

    switch (_paper_type)
    {
    // Entire record contents are written, even data after the ATASCII_EOL
    case RAW:
        for (i = 0; i < n; i++)
            fputc(buffer[i], _file);
        break;
    // Everything up to and including the ATASCII_EOL is written without modification
    case TRIM:
        for (i = 0; i < n; i++)
         {
            fputc(buffer[i], _file);
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
#ifdef BUILD_APPLE
            buffer[i] &= 0x7F; // Strip off high bit.
#endif /* BUILD_APPLE */
            if (buffer[i] == ATASCII_EOL)
            {
                fputs(ASCII_CRLF, _file);
                break;
            }
            // If it's an inverse character, convert to normal
            char c = ATASCII_REMOVE_INVERSE(buffer[i]);
            // If it's a printable character, just copy it
            if(c >=32 && c <= 122 && c != 96)            
            {
                fputc(c, _file);
            }
         }
    }
    return true;
}
