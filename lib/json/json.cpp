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
    _protocol=newProtocol;
}

/**
 * Parse data from protocol
 */
bool JSON::parse()
{
    char* buf;

    if (_protocol==nullptr)
        return false;
    
    if (_protocol->available()==0)
        return false;

#ifdef BOARD_HAS_PSRAM
    buf = (char *)heap_caps_malloc(_protocol->available(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    buf = (char *)calloc(1, _protocol->available());
#endif
    if (buf == nullptr)
        return false;

    if (_protocol->read((uint8_t *)buf,_protocol->available())==false)
        return false;

    _json = cJSON_Parse(buf);

    if (_json == nullptr)
        return false;

    Debug_printf("Parsed JSON: %s\n",cJSON_Print(_json));

    free(buf);
    return true;
}