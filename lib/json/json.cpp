/**
 * JSON Wrapper for #FujiNet
 * 
 * Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

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
 * Parse data from protocol
 */
bool JSON::parse()
{
    char *buf;
    int available=0;

    if (_protocol == nullptr)
    {
        Debug_printf("JSON::parse() - NULL protocol.\n");
        return false;
    }

    //while (available==0)
        available=_protocol->available();

    Debug_printf("JSON::parse() - %d bytes now available\n",available);

#ifdef BOARD_HAS_PSRAM
    buf = (char *)heap_caps_malloc(available, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    buf = (char *)calloc(1, available);
#endif
    if (buf == nullptr)
    {
        Debug_printf("JSON::parse() - could not allocate JSON buffer of %d bytes",available);
        return false;
    }

    if (_protocol->read((uint8_t *)buf, available) == true)
    {
        Debug_printf("JSON::parse() - Could not read %d bytes from protocol adapter.\n",available);
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