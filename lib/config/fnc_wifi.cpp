#include "fnConfig.h"
#include <cstring>
#include "../../include/debug.h"

/* Replaces stored SSID with up to num_octets bytes, but stops if '\0' is reached
*/
void fnConfig::store_wifi_ssid(const char *ssid_octets, int num_octets)
{
    if (_wifi.ssid.compare(0, num_octets, ssid_octets) == 0)
        return;

    Debug_println("new SSID provided");

    _dirty = true;
    _wifi.ssid.clear();
    for (int i = 0; i < num_octets; i++)
    {
        if (ssid_octets[i] == '\0')
            break;
        else
            _wifi.ssid += ssid_octets[i];
    }
}

/* Replaces stored passphrase with up to num_octets bytes, but stops if '\0' is reached
*/
void fnConfig::store_wifi_passphrase(const char *passphrase_octets, int num_octets)
{
    if (_wifi.passphrase.compare(0, num_octets, passphrase_octets) == 0)
        return;
    _dirty = true;
    _wifi.passphrase.clear();
    for (int i = 0; i < num_octets; i++)
    {
        if (passphrase_octets[i] == '\0')
            break;
        else
            _wifi.passphrase += passphrase_octets[i];
    }
}

/* Stores whether Wifi is enabled or not */
void fnConfig::store_wifi_enabled(bool status)
{
    _wifi.enabled = status;
    _dirty = true;
}

void fnConfig::_read_section_wifi(std::stringstream &ss)
{
    // Throw out any existing data
    _wifi.ssid.clear();
    _wifi.passphrase.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "SSID") == 0)
            {
                _wifi.ssid = value;
            }
            else if (strcasecmp(name.c_str(), "passphrase") == 0)
            {
                _wifi.passphrase = value;
            }
            else if (strcasecmp(name.c_str(), "enabled") == 0)
            {
                if (strcasecmp(value.c_str(), "1") == 0)
                    _wifi.enabled = true;
                else
                    _wifi.enabled = false;
            }

        }
    }
}
