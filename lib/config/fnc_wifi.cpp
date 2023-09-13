#include "fnConfig.h"
#include "fnWiFi.h"
#include "crypt.h"
#include "string_utils.h"
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
    if (_general.encrypt_passphrase) {
        _wifi.passphrase = crypto.crypt(_wifi.passphrase);
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
    Debug_println("Reading wifi section");

    // Throw out any existing data
    reset_wifi();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        // Debug_printf("wifi line: >%s<\r\n", line.c_str());
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            // Debug_printf(" name: >%s<\r\n", name.c_str());
            // Debug_printf("value: >%s<\r\n", value.c_str());

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

void fnConfig::_read_section_wifi_stored(std::stringstream &ss, int index)
{
    Debug_printf("Reading stored wifi section for index: %d\r\n", index);

    _wifi_stored[index].ssid.clear();
    _wifi_stored[index].passphrase.clear();
    _wifi_stored[index].enabled = false;

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        // If there's a section, it means it's 'enabled' - we're borrowing the wifi_info structure for alternate purpose
        _wifi_stored[index].enabled = true;

        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "SSID") == 0)
            {
                _wifi_stored[index].ssid = value;
            }
            else if (strcasecmp(name.c_str(), "passphrase") == 0)
            {
                _wifi_stored[index].passphrase = value;
            }
        }
    }
}

void fnConfig::store_wifi_stored_ssid(int index, const std::string &ssid)
{ 
    _wifi_stored[index].ssid = ssid;
    _dirty = true;
}

void fnConfig::store_wifi_stored_passphrase(int index, const std::string &passphrase)
{
    // TODO: check if encryption is an issue here. Should be coming from previous "current" config, which will already be encrypted if enabled.
    _wifi_stored[index].passphrase = passphrase;
    _dirty = true;
}

void fnConfig::store_wifi_stored_enabled(int index, bool enabled)
{ 
    _wifi_stored[index].enabled = enabled;
    _dirty = true;
}
