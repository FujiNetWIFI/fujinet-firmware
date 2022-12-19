#include "fnConfig.h"
#include <cstring>

void fnConfig::store_bt_status(bool status)
{
    _bt.bt_status = status;
    _dirty = true;
}

void fnConfig::store_bt_baud(int baud)
{
    _bt.bt_baud = baud;
    _dirty = true;
}

void fnConfig::store_bt_devname(std::string devname)
{
    _bt.bt_devname = devname;
    _dirty = true;
}

void fnConfig::_read_section_bt(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "enabled") == 0)
            {
                if (strcasecmp(value.c_str(), "1") == 0)
                    _bt.bt_status = true;
                else
                    _bt.bt_status = false; 
            }
            else if (strcasecmp(name.c_str(), "baud") == 0)
            {
                _bt.bt_baud = stoi(value);
            }
            else if (strcasecmp(name.c_str(), "devicename") == 0)
            {
                _bt.bt_devname = value;
            }
        }
    }
}
