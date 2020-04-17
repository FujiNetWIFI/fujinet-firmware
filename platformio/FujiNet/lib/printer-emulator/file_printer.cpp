#include "file_printer.h"
#include "debug.h"

// for Atari EOL
#define EOL 155

void filePrinter::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);
}

bool filePrinter::process(const byte *B, byte n)
{
    int i = 0;
    //std::string output = std::string();

    switch (paperType)
    {
    case RAW:
        for (i = 0; i < n; i++)
        {
            _file.write(B[i]);
            Debug_print(B[i], HEX);
        }
        Debug_printf("\n");
        break;
    case TRIM:
        while (i < n)
        {
            _file.write(B[i]);
            Debug_print(B[i], HEX);
            if (B[i] == EOL)
            {
                Debug_printf("\n");
                break;
            }
            i++;
        }
        break;
    case ASCII:
    default:
        while (i < n)
        {
            if (B[i] == EOL)
            {
                _file.printf("\n");
                Debug_printf("\n");
                break;
            }
            if (B[i] > 31 && B[i] < 127)
            {
                _file.write(B[i]);
                Debug_printf("%c", B[i]);
            }
            i++;
        }
    }
    return true;
}