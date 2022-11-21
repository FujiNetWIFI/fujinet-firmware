#include "fnConfig.h"
#include <cstring>

std::string fnConfig::get_host_name(uint8_t num)
{
    if (num < MAX_HOST_SLOTS)
        return _host_slots[num].name;
    else
        return "";
}

fnConfig::host_type_t fnConfig::get_host_type(uint8_t num)
{
    if (num < MAX_HOST_SLOTS)
        return _host_slots[num].type;
    else
        return host_type_t::HOSTTYPE_INVALID;
}

void fnConfig::store_host(uint8_t num, const char *hostname, host_type_t type)
{
    if (num < MAX_HOST_SLOTS)
    {
        if (_host_slots[num].type == type && _host_slots[num].name.compare(hostname) == 0)
            return;
        _dirty = true;
        _host_slots[num].type = type;
        _host_slots[num].name = hostname;
    }
}

void fnConfig::clear_host(uint8_t num)
{
    if (num < MAX_HOST_SLOTS)
    {
        if (_host_slots[num].type == HOSTTYPE_INVALID && _host_slots[num].name.length() == 0)
            return;
        _dirty = true;
        _host_slots[num].type = HOSTTYPE_INVALID;
        _host_slots[num].name.clear();
    }
}

void fnConfig::_read_section_host(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    _host_slots[index].type = HOSTTYPE_INVALID;
    _host_slots[index].name.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "name") == 0)
            {
                _host_slots[index].name = value;
            }
            else if (strcasecmp(name.c_str(), "type") == 0)
            {
                _host_slots[index].type = host_type_from_string(value.c_str());
            }
        }
    }
}
