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