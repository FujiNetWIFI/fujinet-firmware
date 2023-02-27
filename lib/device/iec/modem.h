#ifdef BUILD_IEC
#ifndef MODEM_H
#define MODEM_H

#include "bus.h"

#include "modem-sniffer.h"
#include <cstdint>

class iecModem : public virtualDevice
{
private:
    ModemSniffer* modemSniffer;
    FileSystem *activeFS;
    time_t _lasttime;
    
public:
    iecModem(FileSystem *_fs, bool snifferEnable);
    virtual ~iecModem();
    void status() override;
    device_state_t process(IECData *commanddata) override;

    ModemSniffer *get_modem_sniffer() { return modemSniffer; }
    time_t get_last_activity_time() { return _lasttime; } // timestamp of last input or output.

};

#endif
#endif