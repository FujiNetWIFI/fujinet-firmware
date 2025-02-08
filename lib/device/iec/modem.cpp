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
        modemSniffer = nullptr;
    }
}

// device_state_t iecModem::process()
// {
//     // TODO IMPLEMENT
//     return DEVICE_IDLE;
// }

#endif /* BUILD_IEC */