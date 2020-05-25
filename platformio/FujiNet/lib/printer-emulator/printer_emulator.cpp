#include "../../include/debug.h"
#include "printer_emulator.h"

#include "fnFsSPIF.h"

#define PRINTER_OUTFILE "/paper"

// initialzie printer by creating an output file
void printer_emu::initPrinter(FileSystem *fs, paper_t ptype = RAW)
{
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
#define PRINTER_FILE_COPY_BUFLEN 2048

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

// All the work is done here in the derived classes. Open and close the output file before proceeding
bool printer_emu::process(byte linelen, byte aux1, byte aux2)
{
    return process_buffer(linelen, aux1, aux2);
}

// This is only called from the HTTP server to request the file be closed before sending it to the user
void printer_emu::pageEject()
{
    if(_file != nullptr)
        fclose(_file);

    _file = _FS->file_open(PRINTER_OUTFILE, "a"); // Append

    pre_close_file();
    
    fflush(_file);
    fseek(_file, 0, SEEK_SET);
}

void printer_emu::resetOutput()
{
    if(_file != nullptr)
        fclose(_file);
    _file = _FS->file_open(PRINTER_OUTFILE, "w+");
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
