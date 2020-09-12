/**
 * JSON Wrapper for #FujiNet
 * 
 * Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#include "json.h"
#include "debug.h"

/**
 * ctor
 */
JSON::JSON()
{
    Debug_printf("JSON::ctor()\n");
    _protocol = nullptr;
}

/**
 * dtor
 */
JSON::~JSON()
{
    Debug_printf("JSON::dtor()\n");
    _protocol = nullptr;
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
    if (_protocol==nullptr)
        return false; 
}