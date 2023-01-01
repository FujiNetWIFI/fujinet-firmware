#include "fnConfig.h"
#include <cstring>
#include "utils.h"
#include "../../include/debug.h"

void fnConfig::store_general_devicename(const char *devicename)
{
    if (_general.devicename.compare(devicename) == 0)
        return;

    _general.devicename = devicename;
    _dirty = true;
}

void fnConfig::store_general_timezone(const char *timezone)
{
    if (_general.timezone.compare(timezone) == 0)
        return;

    _general.timezone = timezone;
    _dirty = true;
}

void fnConfig::store_general_rotation_sounds(bool rotation_sounds)
{
    if (_general.rotation_sounds == rotation_sounds)
        return;

    _general.rotation_sounds = rotation_sounds;
    _dirty = true;
}

void fnConfig::store_general_config_enabled(bool config_enabled)
{
    if (_general.config_enabled == config_enabled)
        return;

    _general.config_enabled = config_enabled;
    _dirty = true;
}
void fnConfig::store_general_status_wait_enabled(bool status_wait_enabled)
{
    if (_general.status_wait_enabled == status_wait_enabled)
        return;

    _general.status_wait_enabled = status_wait_enabled;
    _dirty = true;
}
void fnConfig::store_general_encrypt_passphrase(bool encrypt_passphrase)
{
    if (_general.encrypt_passphrase == encrypt_passphrase)
        return;

    // It changed, so either we were encrypting before and it needs decrypting, or v.v.
    // Either way, we will simply reverse the buffer, as enc/dec are isomorphic
    _wifi.passphrase = crypto.crypt(_wifi.passphrase);
    // Debug_printf("fnConfig::store_general_encrypt_passphrase passphrase is now: >%s<\n", _wifi.passphrase.c_str());
    // Debug_printf("fnConfig::store_general_encrypt_passphrase setting _general.encrypt_passphrase to %d\n", encrypt_passphrase);
    _general.encrypt_passphrase = encrypt_passphrase;
    _dirty = true;
    
}
bool fnConfig::get_general_encrypt_passphrase()
{
    return _general.encrypt_passphrase;
}
void fnConfig::store_general_boot_mode(uint8_t boot_mode)
{
    if (_general.boot_mode == boot_mode)
        return;
    
    _general.boot_mode = boot_mode;
    _dirty = true;
}

void fnConfig::store_general_hsioindex(int hsio_index)
{
    if (_general.hsio_index == hsio_index)
        return;

    _general.hsio_index = hsio_index;
    _dirty = true;
}

void fnConfig::store_general_fnconfig_spifs(bool fnconfig_spifs)
{
    if (_general.fnconfig_spifs == fnconfig_spifs)
        return;

    _general.fnconfig_spifs = fnconfig_spifs;
    _dirty = true;
}

// Saves ENABLE or DISABLE printer
void fnConfig::store_printer_enabled(bool printer_enabled)
{
    if (_general.printer_enabled == printer_enabled)
        return;

    _general.printer_enabled = printer_enabled;
    _dirty = true;
}

void fnConfig::_read_section_general(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "devicename") == 0)
            {
                _general.devicename = value;
            }
            else if (strcasecmp(name.c_str(), "hsioindex") == 0)
            {
                int index = atoi(value.c_str());
                if (index >= 0 && index < 10)
                    _general.hsio_index = index;
            }
            else if (strcasecmp(name.c_str(), "timezone") == 0)
            {
                _general.timezone = value;
            }
            else if (strcasecmp(name.c_str(), "rotationsounds") == 0)
            {
                _general.rotation_sounds = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "configenabled") == 0)
            {
                _general.config_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "boot_mode") == 0)
            {
                int mode = atoi(value.c_str());
                _general.boot_mode = mode;
            }
            else if (strcasecmp(name.c_str(), "fnconfig_on_spifs") == 0)
            {
                _general.fnconfig_spifs = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "status_wait_enabled") == 0)
            {
                _general.status_wait_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "printer_enabled") == 0)
            {
                _general.printer_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "encrypt_passphrase") == 0)
            {
                _general.encrypt_passphrase = util_string_value_is_true(value);
            }
        }
    }
}
