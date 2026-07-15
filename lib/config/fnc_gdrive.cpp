#include "fnConfig.h"
#include <cstring>

void fnConfig::store_gdrive_refresh_token(const std::string &refresh_token)
{
    if (_gdrive.refresh_token == refresh_token)
        return;
    _gdrive.refresh_token = refresh_token;
    _dirty = true;
}

void fnConfig::store_gdrive_access_token(const std::string &access_token)
{
    if (_gdrive.access_token == access_token)
        return;
    _gdrive.access_token = access_token;
    _dirty = true;
}

void fnConfig::store_gdrive_token_expiry(long expiry)
{
    if (_gdrive.token_expiry == expiry)
        return;
    _gdrive.token_expiry = expiry;
    _dirty = true;
}

void fnConfig::_read_section_gdrive(std::stringstream &ss)
{
    std::string line;

    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "refresh_token") == 0)
                _gdrive.refresh_token = value;
            else if (strcasecmp(name.c_str(), "access_token") == 0)
                _gdrive.access_token = value;
            else if (strcasecmp(name.c_str(), "token_expiry") == 0)
                _gdrive.token_expiry = atol(value.c_str());
        }
    }
}
