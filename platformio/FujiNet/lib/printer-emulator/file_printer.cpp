#include "file_printer.h"
#include "debug.h"

// for Atari EOL
#define EOL 155

void filePrinter::initPrinter(FS *filesystem)
{
    printer_emu::initPrinter(filesystem);
}

bool filePrinter::process(byte n)
{
    int i = 0;
    //std::string output = std::string();

    switch (paperType)
    {
    case RAW:
        for (i = 0; i < n; i++)
        {
            _file.write(buffer[i]);
            Debug_print(buffer[i], HEX);
        }
        Debug_printf("\n");
        break;
    case TRIM:
        while (i < n)
        {
            _file.write(buffer[i]);
            Debug_print(buffer[i], HEX);
            if (buffer[i] == EOL)
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
            if (buffer[i] == EOL)
            {
                _file.printf("\n");
                Debug_printf("\n");
                break;
            }
            if (buffer[i] > 31 && buffer[i] < 127)
            {
                _file.write(buffer[i]);
                Debug_printf("%c", buffer[i]);
            }
            i++;
        }
    }
    return true;
}

void filePrinter::pageEject()
{
    printer_emu::pageEject();
}