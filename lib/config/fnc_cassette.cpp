#include "fnConfig.h"
#include <cstring>
#include "utils.h"

bool fnConfig::get_cassette_buttons()
{
    return _cassette.button;
}

bool fnConfig::get_cassette_pulldown()
{
    return _cassette.pulldown;
}

bool fnConfig::get_cassette_enabled()
{
    return _cassette.cassette_enabled;
}

void fnConfig::store_cassette_buttons(bool button)
{
    if (_cassette.button != button)
    {
        _cassette.button = button;
        _dirty = true;
    }
}

void fnConfig::store_cassette_pulldown(bool pulldown)
{
    if (_cassette.pulldown != pulldown)
    {
        _cassette.pulldown = pulldown;
        _dirty = true;
    }
}

void fnConfig::store_cassette_enabled(bool cassette_enabled)
{
    if (_cassette.cassette_enabled != cassette_enabled)
    {
        _cassette.cassette_enabled = cassette_enabled;
        _dirty = true;
    }
}

void fnConfig::_read_section_cassette(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "play_record") == 0)
            {
                _cassette.button = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "pulldown") == 0)
            {
                _cassette.pulldown = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "cassette_enabled") == 0)
            {
                _cassette.cassette_enabled = util_string_value_is_true(value);
            }
        }
    }
}

