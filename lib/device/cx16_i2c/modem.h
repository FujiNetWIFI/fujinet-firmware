#ifdef BUILD_CX16
#ifndef MODEM_H
#define MODEM_H

#include "bus.h"

#include "modem-sniffer.h"
#include <cstdint>

class cx16Modem : public virtualDevice
{
private:
    ModemSniffer* modemSniffer;
    FileSystem *activeFS;
    time_t _lasttime;
    
public:
    cx16Modem(FileSystem *_fs, bool snifferEnable);
    virtual ~cx16Modem();

    ModemSniffer *get_modem_sniffer() { return modemSniffer; }
    time_t get_last_activity_time() { return _lasttime; } // timestamp of last input or output.

};

#endif
#endif