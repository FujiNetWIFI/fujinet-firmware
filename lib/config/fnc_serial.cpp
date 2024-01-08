#ifndef ESP_PLATFORM

#include "fnConfig.h"
#include <cstring>
#include "fnSystem.h"
#include "utils.h"
#include "debug.h"

void fnConfig::store_serial_port(const char *port)
{
    if (_serial.port.compare(port) == 0)
        return;

    _serial.port = port;
    _dirty = true;
}

void fnConfig::store_serial_command(serial_command_pin command_pin)
{
    if (command_pin < 0 || command_pin >= SERIAL_COMMAND_INVALID || _serial.command == command_pin)
        return;

    _serial.command = command_pin;
    _dirty = true;
}

void fnConfig::store_serial_proceed(serial_proceed_pin proceed_pin)
{
    if (proceed_pin < 0 || proceed_pin >= SERIAL_PROCEED_INVALID || _serial.proceed == proceed_pin)
        return;

    _serial.proceed = proceed_pin;
    _dirty = true;
}

void fnConfig::store_netsio_enabled(bool enabled) {
    if (_netsio.netsio_enabled == enabled)
        return;

    _netsio.netsio_enabled = enabled;
    _dirty = true;
}

void fnConfig::store_netsio_host(const char *host) {
    if (_netsio.host.compare(host) == 0)
        return;

    _netsio.host = host;
    _dirty = true;
}

void fnConfig::store_netsio_port(int port) {
    if (_netsio.port == port)
        return;

    _netsio.port = port;
    _dirty = true;
}

void fnConfig::store_boip_enabled(bool enabled) {
    if (_boip.boip_enabled == enabled)
        return;

    _boip.boip_enabled = enabled;
    _dirty = true;
}

void fnConfig::store_boip_host(const char *host) {
    if (_boip.host.compare(host) == 0)
        return;

    _boip.host = host;
    _dirty = true;
}

void fnConfig::store_boip_port(int port) {
    if (_boip.port == port)
        return;

    _boip.port = port;
    _dirty = true;
}

void fnConfig::_read_section_serial(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "port") == 0)
            {
                _serial.port = value;
            }
            else if (strcasecmp(name.c_str(), "command") == 0)
            {
                _serial.command = serial_command_from_string(value.c_str());
            }
            else if (strcasecmp(name.c_str(), "proceed") == 0)
            {
                _serial.proceed = serial_proceed_from_string(value.c_str());
            }
        }
    }
}

void fnConfig::_read_section_netsio(std::stringstream &ss)
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
                _netsio.netsio_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "host") == 0)
            {
                _netsio.host = value;
            }
            else if (strcasecmp(name.c_str(), "port") == 0)
            {
                int port = atoi(value.c_str());
                if (port <= 0 || port > 65535) 
                    port = CONFIG_DEFAULT_NETSIO_PORT;
                _netsio.port = port;
            }
        }
    }
}

void fnConfig::_read_section_boip(std::stringstream &ss)
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
                _boip.boip_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "host") == 0)
            {
                _boip.host = value;
            }
            else if (strcasecmp(name.c_str(), "port") == 0)
            {
                int port = atoi(value.c_str());
                if (port <= 0 || port > 65535) 
                    port = CONFIG_DEFAULT_NETSIO_PORT;
                _boip.port = port;
            }
        }
    }
}

fnConfig::serial_command_pin fnConfig::serial_command_from_string(const char *str)
{
    int i = 0;
    for (; i < serial_command_pin::SERIAL_COMMAND_INVALID; i++)
        if (strcasecmp(_serial_command_pin_names[i], str) == 0)
            break;
    return (serial_command_pin)i;
}

fnConfig::serial_proceed_pin fnConfig::serial_proceed_from_string(const char *str)
{
    int i = 0;
    for (; i < serial_proceed_pin::SERIAL_PROCEED_INVALID; i++)
        if (strcasecmp(_serial_proceed_pin_names[i], str) == 0)
            break;
    return (serial_proceed_pin)i;
}

#endif // !ESP_PLATFORM