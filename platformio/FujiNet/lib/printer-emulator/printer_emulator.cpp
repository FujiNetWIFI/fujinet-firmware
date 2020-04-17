#include "printer_emulator.h"
#include "debug.h"

// initialzie printer by creating an output file
void printer_emu::initPrinter(FS *filesystem)
{
    _FS = filesystem;
    this->resetOutput();
}

// virtual void flushOutput(); // do this in pageEject

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

void printer_emu::resetOutput()
{
    _file.close();
    _file = _FS->open("/paper", "w+");
#ifdef DEBUG
    if (_file)
    {
        Debug_println("Printer output file (re)opened");
    }
    else
    {
        Debug_println("Error opening printer file");
    }
#endif
}