#include "fnConfig.h"
#include <cstring>

void fnConfig::store_udpstream_host(const char host_ip[64])
{
    strlcpy(_network.udpstream_host, host_ip, sizeof(_network.udpstream_host));
}

void fnConfig::store_udpstream_port(int port)
{
    _network.udpstream_port = port;
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
        }
    }
}
