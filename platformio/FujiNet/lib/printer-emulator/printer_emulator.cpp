#include "printer_emulator.h"
#include "../../include/debug.h"

// initialzie printer by creating an output file
void printer_emu::initPrinter(FS *filesystem)
{
    _FS = filesystem;
    this->resetOutput();
}


// destructor must be specified for the base class even though it's virtual
printer_emu::~printer_emu()
{
#ifdef DEBUG
    Debug_println("~printer_emu");
#endif
    if(_file)
        _file.close();
}

// virtual void flushOutput(); // do this in pageEject

// Copy contents of given file to the current printer output file
size_t printer_emu::copy_file_to_output(const char *filename)
{
#define PRINTER_FILE_COPY_BUFLEN 4096

    File fInput = _FS->open(filename, "r");

    if (!fInput || !fInput.available())
    {
#ifdef DEBUG
        Debug_printf("Failed to open printer concatenation file: '%s'\n", filename);
#endif
        return 0;
    }

    // Copy the file content in chunks
    uint8_t *buf = (uint8_t *)malloc(PRINTER_FILE_COPY_BUFLEN);
    size_t total = 0, count = 0;
    do
    {
        count = fInput.read(buf, PRINTER_FILE_COPY_BUFLEN);
        total += _file.write(buf, count);
    } while (count > 0);
    fInput.close();

    free(buf);

    return total;
}

void printer_emu::copyChar(byte c, byte n)
{
    buffer[n] = c;
}

size_t printer_emu::getOutputSize()
{
    return _file.size();
}

int printer_emu::readFromOutput()
{
    return _file.read();
}

int printer_emu::readFromOutput(uint8_t *buf, size_t size)
{
    return _file.read(buf, size);
}

void printer_emu::pageEject()
{
    this->pre_page_eject();
    
    _file.flush();
    _file.seek(0);
}

void printer_emu::resetOutput()
{
    _file.close();
    _file = _FS->open("/paper", "w+");
#ifdef DEBUG
    if (_file)
    {
        Debug_println("Printer output file (re)opened");
        this->post_new_file();
    }
    else
    {
        Debug_println("Error opening printer file");
    }
#endif
}