#include "fnConfig.h"
#include <cstring>
#include "fnSystem.h"
#include "utils.h"

#include "../../include/debug.h"

// Fujinet-ESP & FujiNet-PC

// Bus Over IP configuration - used by CoCo and Apple (TODO consider to move Atari here too)

/*
 * TODO! Create a Web configuration section for the Bus Over IP and Bus Over Serial data so it can be configured from WebUI.
*/

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

// Bus Over IP configuration - used by CoCo and Apple (TODO consider to move Atari here too)
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
                    port = CONFIG_DEFAULT_BOIP_PORT;
                _boip.port = port;
            }
        }
    }
}

#ifdef BUILD_RS232
void fnConfig::_read_section_rs232(std::stringstream &ss)
{
    std::string line;

    while (_read_line(ss,line,'[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line,name,value))
        {
            if (strcasecmp(name.c_str(),"baud") == 0)
            {
                int baud = atoi(value.c_str());
                if (baud<=0 || baud>= INT_MAX)
                {
                    baud = CONFIG_DEFAULT_RS232_BAUD;
                }
                    _rs232.baud = baud;
            }
        }
    }
}

void fnConfig::store_rs232_baud(int _baud) {
    if (_baud == _rs232.baud)
        return;

    _rs232.baud = _baud;
    _dirty = true;
}
#endif

#ifndef ESP_PLATFORM

// FujiNet-PC specific settings

// Serial port settings

void fnConfig::store_serial_port(const char *port)
{
    if (_serial.port.compare(port) == 0)
        return;

    _serial.port = port;
    _dirty = true;
}

void fnConfig::store_serial_baud(int baud)
{
    if (_serial.baud == baud)
        return;

    _serial.baud = baud;
    _dirty = true;
}

// ATARI specific - maps PC UART signal to SIO Command signal
void fnConfig::store_serial_command(serial_command_pin command_pin)
{
    if (command_pin < 0 || command_pin >= SERIAL_COMMAND_INVALID || _serial.command == command_pin)
        return;

    _serial.command = command_pin;
    _dirty = true;
}

// ATARI specific - maps PC UART signal to SIO Proceed signal
void fnConfig::store_serial_proceed(serial_proceed_pin proceed_pin)
{
    if (proceed_pin < 0 || proceed_pin >= SERIAL_PROCEED_INVALID || _serial.proceed == proceed_pin)
        return;

    _serial.proceed = proceed_pin;
    _dirty = true;
}

void fnConfig::store_bos_enabled(bool bos_enabled) {
    if (_bos.bos_enabled == bos_enabled)
        return;
    
    _bos.bos_enabled = bos_enabled;
    _dirty = true;
}

void fnConfig::store_bos_port_name(char *port_name) {
    if (_bos.port_name.compare(port_name) == 0)
        return;
    
    _bos.port_name = port_name;
    _dirty = true;
}

void fnConfig::store_bos_baud(int baud) {
    if (_bos.baud == baud)
        return;
    
    _bos.baud = baud;
    _dirty = true;
}

void fnConfig::store_bos_bits(int bits) {
    if (_bos.bits == bits)
        return;
    
    _bos.bits = bits;
    _dirty = true;
}

void fnConfig::store_bos_parity(int parity) {
    if (_bos.parity == parity)
        return;
    
    _bos.parity = parity;
    _dirty = true;
}

void fnConfig::store_bos_stop_bits(int stop_bits) {
    if (_bos.stop_bits == stop_bits)
        return;
    
    _bos.stop_bits = stop_bits;
    _dirty = true;
}

void fnConfig::store_bos_flowcontrol(int flowcontrol) {
    if (_bos.flowcontrol == flowcontrol)
        return;
    
    _bos.flowcontrol = flowcontrol;
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
            else if (strcasecmp(name.c_str(), "baud") == 0)
            {
                _serial.baud = atoi(value.c_str());
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

void fnConfig::_read_section_bos(std::stringstream &ss)
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
                _bos.bos_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "port_name") == 0)
            {
                _bos.port_name = value;
            }
            else if (strcasecmp(name.c_str(), "baud") == 0)
            {
                int baud = atoi(value.c_str());
                _bos.baud = baud;
            }
            else if (strcasecmp(name.c_str(), "bits") == 0)
            {
                int bits = atoi(value.c_str());
                _bos.bits = bits;
            }
            else if (strcasecmp(name.c_str(), "parity") == 0)
            {
                int parity = atoi(value.c_str());
                _bos.parity = parity;
            }
            else if (strcasecmp(name.c_str(), "stop_bits") == 0)
            {
                int stop_bits = atoi(value.c_str());
                _bos.stop_bits = stop_bits;
            }
            else if (strcasecmp(name.c_str(), "flowcontrol") == 0)
            {
                int flowcontrol = atoi(value.c_str());
                _bos.flowcontrol = flowcontrol;
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
