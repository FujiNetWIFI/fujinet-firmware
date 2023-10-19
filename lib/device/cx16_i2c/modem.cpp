#ifdef BUILD_CX16

#include "modem.h"

cx16Modem::cx16Modem(FileSystem *_fs, bool snifferEnable)
    : activeFS(_fs)
    , modemSniffer(new ModemSniffer(activeFS, snifferEnable))
{
}

cx16Modem::~cx16Modem()
{
    delete modemSniffer;
    modemSniffer = nullptr;
}

#endif /* BUILD_CX16 */
