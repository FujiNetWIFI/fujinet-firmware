#include "fnConfig.h"
#include <cstring>

void fnConfig::store_device_slot_enable_1(bool enable)
{
    if (_denable.device_1_enabled != enable)
    {
        _denable.device_1_enabled = enable;
        _dirty = true;
    }
}

void fnConfig::store_device_slot_enable_2(bool enable)
{
    if (_denable.device_2_enabled != enable)
    {
        _denable.device_2_enabled = enable;
        _dirty = true;
    }
}

void fnConfig::store_device_slot_enable_3(bool enable)
{
    if (_denable.device_3_enabled != enable)
    {
        _denable.device_3_enabled = enable;
        _dirty = true;
    }
}

void fnConfig::store_device_slot_enable_4(bool enable)
{
    if (_denable.device_4_enabled != enable)
    {
        _denable.device_4_enabled = enable;
        _dirty = true;
    }
}

void fnConfig::store_device_slot_enable_5(bool enable)
{
    if (_denable.device_5_enabled != enable)
    {
        _denable.device_5_enabled = enable;
        _dirty = true;
    }
}

void fnConfig::store_device_slot_enable_6(bool enable)
{
    if (_denable.device_6_enabled != enable)
    {
        _denable.device_6_enabled = enable;
        _dirty = true;
    }
}

void fnConfig::store_device_slot_enable_7(bool enable)
{
    if (_denable.device_7_enabled != enable)
    {
        _denable.device_7_enabled = enable;
        _dirty = true;
    }
}

void fnConfig::store_device_slot_enable_8(bool enable)
{
    if (_denable.device_8_enabled != enable)
    {
        _denable.device_8_enabled = enable;
        _dirty = true;
    }
}

bool fnConfig::get_device_slot_enable_1()
{
    return _denable.device_1_enabled;
}

bool fnConfig::get_device_slot_enable_2()
{
    return _denable.device_2_enabled;
}

bool fnConfig::get_device_slot_enable_3()
{
    return _denable.device_3_enabled;
}

bool fnConfig::get_device_slot_enable_4()
{
    return _denable.device_4_enabled;
}

bool fnConfig::get_device_slot_enable_5()
{
    return _denable.device_5_enabled;
}

bool fnConfig::get_device_slot_enable_6()
{
    return _denable.device_6_enabled;
}

bool fnConfig::get_device_slot_enable_7()
{
    return _denable.device_7_enabled;
}

bool fnConfig::get_device_slot_enable_8()
{
    return _denable.device_8_enabled;
}

bool fnConfig::get_apetime_enabled()
{
    return _denable.apetime;
}

void fnConfig::store_apetime_enabled(bool enabled)
{
    if (_denable.apetime != enabled)
    {
        _denable.apetime = enabled;
        _dirty = true;
    }
}

void fnConfig::_read_section_device_enable(std::stringstream &ss)
{
    std::string line;

    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "enable_device_slot_1") == 0)
                _denable.device_1_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_device_slot_2") == 0)
                _denable.device_2_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_device_slot_3") == 0)
                _denable.device_3_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_device_slot_4") == 0)
                _denable.device_4_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_device_slot_5") == 0)
                _denable.device_5_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_device_slot_6") == 0)
                _denable.device_6_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_device_slot_7") == 0)
                _denable.device_7_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_device_slot_8") == 0)
                _denable.device_8_enabled = atoi(value.c_str());
            else if (strcasecmp(name.c_str(), "enable_apetime") == 0)
                _denable.apetime = atoi(value.c_str());        
        }
    }
}
