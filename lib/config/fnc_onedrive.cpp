#include "fnConfig.h"
#include <cstring>

void fnConfig::store_onedrive_refresh_token(const std::string &refresh_token)
{
    if (_onedrive.refresh_token == refresh_token)
        return;
    _onedrive.refresh_token = refresh_token;
    _dirty = true;
}

void fnConfig::store_onedrive_access_token(const std::string &access_token)
{
    if (_onedrive.access_token == access_token)
        return;
    _onedrive.access_token = access_token;
    _dirty = true;
}

void fnConfig::store_onedrive_token_expiry(long expiry)
{
    if (_onedrive.token_expiry == expiry)
        return;
    _onedrive.token_expiry = expiry;
    _dirty = true;
}

void fnConfig::_read_section_onedrive(std::stringstream &ss)
{
    std::string line;

    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "refresh_token") == 0)
                _onedrive.refresh_token = value;
            else if (strcasecmp(name.c_str(), "access_token") == 0)
                _onedrive.access_token = value;
            else if (strcasecmp(name.c_str(), "token_expiry") == 0)
                _onedrive.token_expiry = atol(value.c_str());
        }
    }
}
