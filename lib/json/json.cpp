/**
 * JSON Wrapper for #FujiNet
 * 
 * Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#include <string.h>
#include <sstream>
#include "json.h"
#include "../../include/debug.h"

/**
 * ctor
 */
JSON::JSON()
{
    Debug_printf("JSON::ctor()\n");
    _protocol = nullptr;
    _json = nullptr;
}

/**
 * dtor
 */
JSON::~JSON()
{
    Debug_printf("JSON::dtor()\n");
    _protocol = nullptr;
    _json = nullptr;
}

/**
 * Attach protocol handler
 */
void JSON::setProtocol(networkProtocol *newProtocol)
{
    Debug_printf("JSON::setProtocol()\n");
    _protocol = newProtocol;
}

/**
 * Set read query string
 */
void JSON::setReadQuery(string queryString)
{
    _queryString = queryString;
}

/**
 * Resolve query string
 */
cJSON *JSON::resolveQuery()
{
    // This needs a full blown query parser!, for now, I just find object on same depth.
    return cJSON_GetObjectItem(_json, _queryString.c_str());
}

/**
 * Return normalized string of JSON item
 */
string JSON::getValue(cJSON *item)
{
    if (cJSON_IsString(item))
        return string(cJSON_GetStringValue(item)) + "\x9b";
    else if (cJSON_IsBool(item))
    {
        if (cJSON_IsTrue(item))
            return "TRUE\x9b";
        else if (cJSON_IsFalse(item))
            return "FALSE\x9b";
    }
    else if (cJSON_IsNull(item))
        return "NULL\x9b";
    else if (cJSON_IsNumber(item))
    {
        stringstream ss;
        ss << item->valuedouble;
        return ss.str() + "\x9b";
    }
    
    return "UNKNOWN\x9b";
}

/**
 * Return requested value
 */
bool JSON::readValue(uint8_t *rx_buf, unsigned short len)
{
    cJSON *item = resolveQuery();
    string ret=getValue(item);

    if (item == nullptr)
        return true; // error

    memcpy(rx_buf,ret.data(),ret.size());

    return false; // no error.
}

/**
 * Return requested value length
 */
int JSON::readValueLen()
{
    cJSON *item = resolveQuery();
    int len = getValue(item).size();

    if (item == nullptr)
        return len;

    return len;
}

/**
 * Parse data from protocol
 */
bool JSON::parse()
{
    char *buf;
    int available = 0;

    if (_protocol == nullptr)
    {
        Debug_printf("JSON::parse() - NULL protocol.\n");
        return false;
    }

    //while (available==0)
    available = _protocol->available();

    Debug_printf("JSON::parse() - %d bytes now available\n", available);

#ifdef BOARD_HAS_PSRAM
    buf = (char *)heap_caps_malloc(available, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    buf = (char *)calloc(1, available);
#endif
    if (buf == nullptr)
    {
        Debug_printf("JSON::parse() - could not allocate JSON buffer of %d bytes", available);
        return false;
    }

    if (_protocol->read((uint8_t *)buf, available) == true)
    {
        Debug_printf("JSON::parse() - Could not read %d bytes from protocol adapter.\n", available);
        return false;
    }

    _json = cJSON_Parse(buf);

    if (_json == nullptr)
    {
        Debug_printf("JSON::parse() - Could not parse JSON\n");
        return false;
    }

    Debug_printf("Parsed JSON: %s\n", cJSON_Print(_json));

    free(buf);
    return true;
}