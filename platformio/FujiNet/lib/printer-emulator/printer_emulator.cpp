#include "printer_emulator.h"
#include "../../include/debug.h"

#include "fnFsSPIF.h"

// initialzie printer by creating an output file
void printer_emu::initPrinter(FileSystem *fs)
{
    _FS = fs;
    resetOutput();
}


// destructor must be specified for the base class even though it's virtual
printer_emu::~printer_emu()
{
#ifdef DEBUG
    //Debug_println("~printer_emu");
#endif
    if(_file != nullptr)
    {
        fclose(_file);
        _file = nullptr;
    }
}

// virtual void flushOutput(); // do this in pageEject

// Copy contents of given file to the current printer output file
// Assumes source file is in SPIFFS
size_t printer_emu::copy_file_to_output(const char *filename)
{
#define PRINTER_FILE_COPY_BUFLEN 4096

    FILE * fInput = fnSPIFFS.file_open(filename);

    if (fInput == nullptr)
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
        count = fread(buf, 1, PRINTER_FILE_COPY_BUFLEN, fInput);
        total += fwrite(buf, 1, count, _file);
    } while (count > 0);
    fclose(fInput);

    free(buf);

    return total;
}

void printer_emu::copyChar(byte c, byte n)
{
    buffer[n] = c;
}

size_t printer_emu::getOutputSize()
{
    return FileSystem::filesize(_file);
}

int printer_emu::readFromOutput()
{
    return fgetc(_file);
}

int printer_emu::readFromOutput(uint8_t *buf, size_t size)
{
    return fread(buf, 1, size, _file);
}

void printer_emu::pageEject()
{
    this->pre_page_eject();
    
    fflush(_file);
    fseek(_file, 0, SEEK_SET);
}

void printer_emu::resetOutput()
{
    if(_file != nullptr)
        fclose(_file);
    _file = _FS->file_open("/paper", "w+");
#ifdef DEBUG
    if (_file != nullptr)
    {
        Debug_println("Printer output file (re)opened");
        post_new_file();
    }
    else
    {
        Debug_println("Error opening printer file");
    }
#endif
}
