#ifdef BUILD_IEC

#include "modem.h"

iecModem::iecModem(FileSystem *_fs, bool snifferEnable)
{
    activeFS = _fs;
    modemSniffer = new ModemSniffer(activeFS, snifferEnable);
}

iecModem::~iecModem()
{
    if (modemSniffer != nullptr)
    {
        delete modemSniffer;
    }
}

void iecModem::status()
{
    // TODO IMPLEMENT
}

void iecModem::process(uint32_t commanddata, uint8_t checksum)
{
    // TODO IMPLEMENT
}

#endif /* BUILD_IEC */