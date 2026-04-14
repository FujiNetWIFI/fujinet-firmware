#include "fnConfig.h"
#include <cstdlib>
#include <cstring>
#include "../device/modem.h"
#include "modem-sniffer.h"
#include "utils.h"

// Saves ENABLE or DISABLE Modem
void fnConfig::store_modem_enabled(bool modem_enabled)
{
    if (_modem.modem_enabled == modem_enabled)
        return;

    _modem.modem_enabled = modem_enabled;
    _dirty = true;
}

// Saves ENABLE or DISABLE Modem Sniffer
void fnConfig::store_modem_sniffer_enabled(bool modem_sniffer_enabled)
{
#ifdef BUILD_ATARI
    ModemSniffer *modemSniffer = sioR->get_modem_sniffer();
    modemSniffer->setEnable(modem_sniffer_enabled);

    if (_modem.sniffer_enabled == modem_sniffer_enabled)
        return;

    _modem.sniffer_enabled = modem_sniffer_enabled;
    _dirty = true;
#endif /* BUILD_ATARI */
}

void fnConfig::store_modem_connect_delay_ms(uint32_t modem_connect_delay_ms)
{
    if (_modem.connect_delay_ms == modem_connect_delay_ms)
        return;

    _modem.connect_delay_ms = modem_connect_delay_ms;
    _dirty = true;
}

void fnConfig::_read_section_modem(std::stringstream &ss)
{
    std::string line;

    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "modem_enabled") == 0)
                _modem.modem_enabled = util_string_value_is_true(value);
            else if (strcasecmp(name.c_str(), "sniffer_enabled") == 0)
                _modem.sniffer_enabled = util_string_value_is_true(value);
            else if (strcasecmp(name.c_str(), "connect_delay_ms") == 0)
                _modem.connect_delay_ms = strtoul(value.c_str(), nullptr, 10);
        }
    }
}
