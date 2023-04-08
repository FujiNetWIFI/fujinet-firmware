#ifndef MODEM_H
#define MODEM_H

#include "bus.h"
#include "fnFS.h"   
#include "modem-sniffer.h"

class cdcModem : public virtualDevice
{
private:
    ModemSniffer* modemSniffer;

public:
    cdcModem(FileSystem *_fs, bool snifferEnable) { modemSniffer = new ModemSniffer(_fs, snifferEnable); };
    virtual ~cdcModem() {};

    time_t get_last_activity_time() { return 0; }
    ModemSniffer *get_modem_sniffer() { return modemSniffer; }
};

#endif /* MODEM_H */
