#include "fnConfig.h"
#include <cstring>
#include "compat_string.h"
#include "utils.h"

void fnConfig::store_netstream_host(const char host_ip[64])
{
    strlcpy(_network.netstream_host, host_ip, sizeof(_network.netstream_host));
}

void fnConfig::store_netstream_port(int port)
{
    _network.netstream_port = port;
}

void fnConfig::store_netstream_register(bool enable)
{
    _network.netstream_register = enable;
}

void fnConfig::store_netstream_mode(int mode)
{
    _network.netstream_mode = mode;
}

void fnConfig::_read_section_network(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "sntpserver") == 0)
            {
                strlcpy(_network.sntpserver, value.c_str(), sizeof(_network.sntpserver));
            }
            else if (strcasecmp(name.c_str(), "netstream_host") == 0)
            {
                strlcpy(_network.netstream_host, value.c_str(), sizeof(_network.netstream_host));
            }
            else if (strcasecmp(name.c_str(), "netstream_port") == 0)
            {
                _network.netstream_port = atoi(value.c_str());
            }
            else if (strcasecmp(name.c_str(), "netstream_mode") == 0)
            {
                std::string mode_value = value;
                util_string_tolower(mode_value);
                _network.netstream_mode = (mode_value == "tcp" || mode_value == "1") ? 1 : 0;
            }
            else if (strcasecmp(name.c_str(), "netstream_register") == 0)
            {
                _network.netstream_register = util_string_value_is_true(value);
            }
        }
    }
}
