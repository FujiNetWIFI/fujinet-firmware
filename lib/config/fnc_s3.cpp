#include "fnConfig.h"
#include <cstring>
#include "utils.h"

void fnConfig::store_s3_endpoint(const std::string &endpoint)
{
    if (_s3.endpoint == endpoint)
        return;
    _s3.endpoint = endpoint;
    _dirty = true;
}

void fnConfig::store_s3_region(const std::string &region)
{
    if (_s3.region == region)
        return;
    _s3.region = region;
    _dirty = true;
}

void fnConfig::store_s3_access_key(const std::string &access_key)
{
    if (_s3.access_key == access_key)
        return;
    _s3.access_key = access_key;
    _dirty = true;
}

void fnConfig::store_s3_secret_key(const std::string &secret_key)
{
    if (_s3.secret_key == secret_key)
        return;
    _s3.secret_key = secret_key;
    _dirty = true;
}

void fnConfig::store_s3_use_ssl(bool use_ssl)
{
    if (_s3.use_ssl == use_ssl)
        return;
    _s3.use_ssl = use_ssl;
    _dirty = true;
}

void fnConfig::_read_section_s3(std::stringstream &ss)
{
    std::string line;

    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "endpoint") == 0)
                _s3.endpoint = value;
            else if (strcasecmp(name.c_str(), "region") == 0)
                _s3.region = value;
            else if (strcasecmp(name.c_str(), "access_key") == 0)
                _s3.access_key = value;
            else if (strcasecmp(name.c_str(), "secret_key") == 0)
                _s3.secret_key = value;
            else if (strcasecmp(name.c_str(), "use_ssl") == 0)
                _s3.use_ssl = util_string_value_is_true(value);
        }
    }
}
