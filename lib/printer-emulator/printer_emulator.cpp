#include "printer_emulator.h"

#include "../../include/debug.h"

#include "fnFsSPIFFS.h"


#define PRINTER_OUTFILE "/paper"

// initialzie printer by creating an output file
void printer_emu::initPrinter(FileSystem *fs)
{
    _FS = fs;
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
    if(_file != nullptr)
        return FileSystem::filesize(_file);

    long result = FileSystem::filesize(PRINTER_OUTFILE);

    return result == -1 ? 0 : result;
}

// All the work is done here in the derived classes. Open and close the output file before proceeding
bool printer_emu::process(uint8_t linelen, uint8_t aux1, uint8_t aux2)
{
    // Make sure the file has been initialized
    if(_output_started == false)
    {
        restart_output();
        // Make sure that worked...
        if(_output_started == false)
            return false;
    }

    // Open output file for appending
    _file = _FS->file_open(PRINTER_OUTFILE, "r+"); // This is supposed to open the file for writing at the end, but reading at the beginnig
    fseek(_file, 0, SEEK_END); // Make sure we're at the end of the file for reading in case the emaulator code expects that

    bool result = process_buffer(linelen, aux1, aux2);

    fflush(_file);
    fclose(_file);
    _file = nullptr;

    return result;
}

// Closes the output file and provides an open read handle to it afterwards
FILE * printer_emu::closeOutputAndProvideReadHandle()
{
    closeOutput();
    return _FS->file_open(PRINTER_OUTFILE);
}

// Closes the output file, giving the printer emulators a chance to provide closing output
void printer_emu::closeOutput()
{
    // Assume there's nothing to do if output hasn't been started
    if (_output_started == false)
        return;

    // Give printer emulator chance to finish output
    if(_file == nullptr)
    {
        _file = _FS->file_open(PRINTER_OUTFILE, "r+"); // Seeks don't work right if we use "append" mode - use "r+"
        fseek(_file, 0, SEEK_END);
    }

    pre_close_file();

    // Close the file    
    fflush(_file);
    fclose(_file);
    _file = nullptr;
    _output_started = false;
}

void printer_emu::restart_output()
{
    _output_started = false;
    if(_file != nullptr)
        fclose(_file);
    _file = _FS->file_open(PRINTER_OUTFILE, "w"); // This should create/truncate the file
#ifdef DEBUG
    if (_file != nullptr)
    {
        Debug_println("Printer output file initialized");
        post_new_file();
        fclose(_file);
        _file = nullptr;
        _output_started = true;
    }
    else
    {
        Debug_println("Error opening printer file");
    }
#endif
}
