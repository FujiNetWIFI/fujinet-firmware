#ifdef BUILD_MAC
#ifndef MAC_MODEM_H
#define MAC_MODEM_H

#include "bus.h"
#include "fnSystem.h"

#include "modem-sniffer.h"

class macModem : public macDevice
{
private:
    FileSystem *activeFS;       // Active Filesystem for ModemSniffer.
    ModemSniffer *modemSniffer; // ptr to modem sniffer.
public:
    macModem(FileSystem *_fs, bool snifferEnable);
    virtual ~macModem();

    ModemSniffer *get_modem_sniffer() { return modemSniffer; };
    time_t get_last_activity_time() { return fnSystem.millis(); };

    void shutdown()override {};
    void process(mac_cmd_t cmd) override {};
};

#endif // guard
#endif // BUILD_MAC
